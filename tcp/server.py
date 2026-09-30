import base64
import socket
import struct
import threading
import queue

from flask import request
from flask import Flask
from flask_sock import Sock

from img_conversions import convert_to_jpeg


# ---------------------------------------------------------------------------
# Frame storage capacities
# ---------------------------------------------------------------------------
MAX_FRAME_STORE_SIZE  = 4
MAX_FRAME_BUFFER_SIZE = MAX_FRAME_STORE_SIZE
MAX_FRAME_QUEUE_SIZE  = MAX_FRAME_STORE_SIZE

# ---------------------------------------------------------------------------
# UDP protocol constants (must mirror esp32_cam.ino)
# ---------------------------------------------------------------------------
# [frame_id:2B][img_len:4I][img_w:2B][img_h:2B][pixfmt:1B]
TCP_FRAME_HDR    = ">HIHHB"
TCP_FRAME_HDR_SZ = struct.calcsize(TCP_FRAME_HDR) # 11

SERVER_TCP_PORT  = 8080

# pixformat_t enum (matches esp_camera.h)
PIXFORMAT = {
    0: "RGB565",
    1: "YUV422",
    2: "YUV420",
    3: "GRAYSCALE",
    4: "JPEG",
    5: "RGB888",
    6: "RAW",
    7: "RGB444",
    8: "RGB555",
    9: "RAW8",
}

# ---------------------------------------------------------------------------
# Receive TCP
# ---------------------------------------------------------------------------

def recv_exact(sock: socket.socket, n: int) -> bytes:
    """Block until exactly n bytes are read, or raise on connection close."""
    buf = bytearray(n)
    view = memoryview(buf)
    received = 0
    while received < n:
        chunk = sock.recv_into(view[received:], n - received)
        if chunk == 0:
            raise ConnectionError("TCP connection closed by peer")
        received += chunk
    return bytes(buf)


def tcp_receiver(frame_queue: queue.Queue) -> None:
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", SERVER_TCP_PORT))
    srv.listen(1)
    print(f"[I] TCP listener on port {SERVER_TCP_PORT}")

    while True:
        conn, addr = srv.accept()
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        print(f"[I] ESP32 connected from {addr}")

        try:
            while True:
                hdr  = recv_exact(conn, TCP_FRAME_HDR_SZ)
                frame_id, img_len, width, height, pixfmt = struct.unpack(TCP_FRAME_HDR, hdr)

                raw = recv_exact(conn, img_len)

                try:
                    frame_queue.put_nowait((width, height, pixfmt, raw))
                except queue.Full:
                    try:    frame_queue.get_nowait()
                    except queue.Empty: pass
                    frame_queue.put_nowait((width, height, pixfmt, raw))

        except (ConnectionError, OSError) as e:
            print(f"[E] TCP connection lost: {e}")
            conn.close()


# ---------------------------------------------------------------------------
# Flask + SocketIO app
# ---------------------------------------------------------------------------
app       = Flask(__name__)
clients   = set()
sock      = Sock(app)


# Frame Queue
# Drop old frames if SocketIO is slow
frame_q: queue.Queue = queue.Queue(maxsize=MAX_FRAME_QUEUE_SIZE)


@app.route("/")
def index():
    heading = "Live ESP32-CAM Image Stream"
    image_type = "image/jpeg"

    return f"""
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>{heading}</title>
        <style>
            body {{ margin: 0; background: #111; display: flex; flex-direction: column; align-items: center; }}
            h1 {{ color: #eee; font-family: sans-serif; margin: 12px 0; }}
            canvas {{ max-width: 100%; display: block; }}
        </style>
    </head>
    <body>
        <h1>{heading}</h1>
        <canvas id="stream"></canvas>
        <script>
            const canvas = document.getElementById('stream');
            const ctx = canvas.getContext('2d');

            const ws = new WebSocket(`ws://${{location.host}}/ws`);
            ws.binaryType = 'arraybuffer';

            ws.onopen  = ()  => console.log  ('[I] WebSocket connected');
            ws.onerror = (e) => console.error('[E] WebSocket error', e);
            ws.onclose = (e) => console.warn ('[W] WebSocket closed', e.code, e.reason);

            // let flag = 0;

            ws.onmessage = (event) => {{
                // if (flag % 100 == 0) console.log(event);
                // else if (flag < 51) flag++;
                createImageBitmap(new Blob([event.data], {{ type: '{image_type}' }}))
                    .then((bitmap) => {{
                        canvas.width = bitmap.width;
                        canvas.height = bitmap.height;
                        ctx.drawImage(bitmap, 0, 0);
                        bitmap.close();
                    }} );
            }} ;
        </script>
    </body>
    </html>
    """


@sock.route('/ws')
def ws_handler(ws):
    clients.add(ws)
    print(f"[I] Browser client ({request.remote_addr}) connected")
    try:
        while True:
            ws.receive()  # blocks, keeps connection alive
    except:
        pass
    finally:
        clients.discard(ws)
        print(f"[W] Connection to browser client ({request.remote_addr}) lost")


def frame_broadcaster() -> None:
    """
    Pulls completed frames from frame_q, converts to JPEG, and emits
    over SocketIO. Runs in its own daemon thread so it never blocks
    the UDP receiver or the Flask request handlers.
    """
    while True:
        try:
            width, height, pixfmt, raw = frame_q.get(timeout=1)
        except queue.Empty:
            continue

        content_type = f"image/{PIXFORMAT.get(pixfmt, 'yuv422').lower()}"
        try:
            jpeg_bytes = convert_to_jpeg(raw, content_type, width, height).getvalue()
        except Exception as e:
            print(f"[E] convert_to_jpeg: {e}")
            continue

        dead = set()
        for ws in list(clients):
            try:
                ws.send(jpeg_bytes)
            except:
                dead.add(ws)
        for ws in dead:
            clients.discard(ws)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    # Flask debug mode uses a reloader that forks a child process.
    # Both parent and child would try to bind UDP port 8080, causing EADDRINUSE.
    # use_reloader=False avoids this; debug=True still gives the interactive
    # debugger and auto-restart on uncaught exceptions.

    # Daemon threads — they exit automatically when the main process does
    threading.Thread(target=tcp_receiver, args=(frame_q,), daemon=True).start()
    threading.Thread(target=frame_broadcaster, daemon=True).start()

    app.run(host="0.0.0.0", port=5000, debug=True, use_reloader=False)
