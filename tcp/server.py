import base64
import socket
import struct
import threading
import queue

from flask import request
from flask import Flask
from flask_socketio import SocketIO

from img_conversions import convert_to_jpeg


# ---------------------------------------------------------------------------
# Frame storage capacities
# ---------------------------------------------------------------------------
MAX_FRAME_STORE_SIZE  = 5
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
socketio  = SocketIO(app, cors_allowed_origins="*")
clients   = set()


# Frame Queue
# Drop old frames if SocketIO is slow
frame_q: queue.Queue = queue.Queue(maxsize=MAX_FRAME_QUEUE_SIZE)


@app.route("/")
def index():
    return """
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>Live Image Stream</title>
        <script src="https://cdnjs.cloudflare.com/ajax/libs/socket.io/4.0.1/socket.io.js"></script>
    </head>
    <body>
        <h1>Live Image Stream</h1>
        <img id="live-image" src="" alt="Streaming Image" style="max-width:100%;">
        <script>
            const socket = io();
            const img = document.getElementById('live-image');
            socket.on('image', (data) => {
                img.src = 'data:image/jpeg;base64,' + data;
            });
        </script>
    </body>
    </html>
    """


@socketio.on("connect")
def handle_connect():
    clients.add(request.sid) # type: ignore


@socketio.on("disconnect")
def handle_disconnect():
    clients.discard(request.sid) # type: ignore


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

        if clients:
            encoded = base64.b64encode(jpeg_bytes).decode("utf-8")
            socketio.emit("image", encoded)


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

    socketio.run(app, host="0.0.0.0", port=5000, debug=True, use_reloader=False)
