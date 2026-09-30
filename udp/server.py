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
MAX_FRAME_STORE_SIZE  = 4
MAX_FRAME_BUFFER_SIZE = MAX_FRAME_STORE_SIZE
MAX_FRAME_QUEUE_SIZE  = MAX_FRAME_STORE_SIZE

# ---------------------------------------------------------------------------
# UDP protocol constants (must mirror esp32_cam.ino)
# ---------------------------------------------------------------------------
UDP_FRAME_HDR    = ">HHH"   # [frame_id:2B][frag_no:2B][total_frags:2B]
UDP_FRAME_HDR_SZ = struct.calcsize(UDP_FRAME_HDR)   # 6
UDP_META_HDR     = ">HHB"   # [img_width:2B][img_height:2B][pixformat:1B]
UDP_META_HDR_SZ  = struct.calcsize(UDP_META_HDR)    # 5
UDP_MTU          = (1400 + UDP_FRAME_HDR_SZ + UDP_META_HDR_SZ) # 1411

# UDP payload sizes (mirror C defines)
UDP_FRAG0_DATA_SIZE = UDP_MTU - UDP_FRAME_HDR_SZ - UDP_META_HDR_SZ # 1400
UDP_FRAGN_DATA_SIZE = UDP_MTU - UDP_FRAME_HDR_SZ                   # 1405

SERVER_UDP_PORT  = 8080

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
# Reassembly buffer for a single in-flight frame
# ---------------------------------------------------------------------------
class FrameBuffer:
    def __init__(self, frame_id: int, total_frags: int,
                 width: int, height: int, pixfmt: int):
        self.frame_id    = frame_id
        self.total_frags = total_frags
        self.width       = width
        self.height      = height
        self.pixfmt      = pixfmt
        self.received    = 0

        # Pre-compute total image size and fragment offsets
        # This is not full image size. It is an upper bound based on fragment sizes
        img_size_ub = UDP_FRAG0_DATA_SIZE + (total_frags - 1) * UDP_FRAGN_DATA_SIZE
        self._buf   = bytearray(img_size_ub)

        # actual bytes written, used for trimming
        self._written = 0

        # Pre-compute byte offset for each fragment
        self._offsets = [0] * total_frags
        
        self._offsets[0] = 0
        for i in range(1, total_frags):
            self._offsets[i] = UDP_FRAG0_DATA_SIZE + (i - 1) * UDP_FRAGN_DATA_SIZE

        self._received_mask = bytearray(total_frags)  # 0/1 per frag, avoid set overhead


    def add_fragment(self, frag_no: int, data: bytes) -> bool:
        if self._received_mask[frag_no]:
            return False # duplicate

        self._received_mask[frag_no] = 1

        off = self._offsets[frag_no]
        n = len(data)

        # copy into pre-allocated buffer
        # in C this copy can probably be avoided, limitations of Python
        self._buf[off:off + n] = data

        self._written += n

        self.received += 1
        return self.received == self.total_frags


    def assemble(self) -> memoryview:
        # memoryview: zero copy slice, no new allocation
        return memoryview(self._buf)[:self._written]


# ---------------------------------------------------------------------------
# UDP receiver — runs in its own daemon thread
# ---------------------------------------------------------------------------
def udp_receiver(frame_queue: queue.Queue) -> None:
    """
    Listens for UDP datagrams, reassembles fragments per frame_id,
    and pushes completed (width, height, pixfmt, raw_bytes) tuples
    onto frame_queue for the main thread to broadcast.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)  # 4 MB kernel buf
    sock.bind(("0.0.0.0", SERVER_UDP_PORT))
    print(f"[I] UDP listener on port {SERVER_UDP_PORT}")

    # frame_id → FrameBuffer  (keep only the most recent frame_id to
    # avoid unbounded growth if a frame is never completed due to packet loss)
    buffers: dict[int, FrameBuffer] = {}

    # pending: dict[int, dict[int, memoryview]] = {}

    prev_frame_id = -1

    while True:
        try:
            pkt, _ = sock.recvfrom(UDP_MTU)
        except OSError as e:
            print(f"[E] UDP recv error: {e}")
            continue

        # ---------------------------------------------------------------
        # Parse frame header
        # ---------------------------------------------------------------
        if len(pkt) < UDP_FRAME_HDR_SZ:
            print("[W] Packet too short for frame header, dropping")
            continue

        frame_id, frag_no, total_frags = struct.unpack_from(UDP_FRAME_HDR, pkt, 0)
        offset = UDP_FRAME_HDR_SZ

        # Parse metadata header (fragment 0 only)
        if frag_no == 0:
            if len(pkt) < offset + UDP_META_HDR_SZ:
                print("[W] Fragment 0 too short for metadata header, dropping")
                continue

            width, height, pixfmt = struct.unpack_from(UDP_META_HDR, pkt, offset)
            offset += UDP_META_HDR_SZ

            if frame_id not in buffers:
                buffers[frame_id] = FrameBuffer(frame_id, total_frags, width, height, pixfmt)

            # Evict oldest if over capacity
            if len(buffers) > MAX_FRAME_BUFFER_SIZE:
                del buffers[min(buffers)]

        # Store fragment data
        # if frame_id not in buffers:
        #     # Non-zero fragment arrived before frag 0 — park it
        #     pending.setdefault(frame_id, {})[frag_no] = memoryview(pkt)[offset:]
        #     continue

        if frame_id not in buffers:
            # skip frame if first fragment is not frag 0
            if frame_id > prev_frame_id:
                print(f"[W] Missing fragment 0, dropped frame {frame_id}")
            prev_frame_id = frame_id
            continue

        buf = buffers[frame_id]

        # Replay any parked fragments now that we have the buffer
        # if frame_id in pending:
        #     for parked_frag_no, parked_data in pending.pop(frame_id).items():
        #         buf.add_fragment(parked_frag_no, parked_data)

        if buf.add_fragment(frag_no, memoryview(pkt)[offset:]):
            raw = buf.assemble()
            # expected_size = buf.width * buf.height
            # if len(raw) != expected_size:
            #     print(f"[W] Frame {frame_id} size mismatch: {len(raw)} != {expected_size}, dropping")
            #     del buffers[frame_id]
            #     continue
            try:
                frame_queue.put_nowait((buf.width, buf.height, buf.pixfmt, raw))
            except queue.Full:
                try:
                    frame_queue.get_nowait()
                except queue.Empty:
                    pass
                frame_queue.put_nowait((buf.width, buf.height, buf.pixfmt, raw))
            del buffers[frame_id]

        # Evict stale pending too
        # if len(pending) > MAX_FRAME_BUFFER_SIZE:
        #     del pending[min(pending)]


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
        <style>
            body { margin: 0; background: #111; display: flex; flex-direction: column; align-items: center; }
            h1 { color: #eee; font-family: sans-serif; margin: 12px 0; }
            canvas { max-width: 100%; display: block; }
        </style>
    </head>
    <body>
        <h1>Live Image Stream</h1>
        <canvas id="stream"></canvas>
        <script>
            const socket = io();
            const canvas = document.getElementById('stream');
            const ctx = canvas.getContext('2d');
            const img = new Image();

            img.onload = () => {
                if (canvas.width !== img.naturalWidth || canvas.height !== img.naturalHeight) {
                    canvas.width  = img.naturalWidth;
                    canvas.height = img.naturalHeight;
                }
                ctx.drawImage(img, 0, 0);
                URL.revokeObjectURL(img.src);   // free memory if using blob, no-op for data URLs
            };

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
    threading.Thread(target=udp_receiver, args=(frame_q,), daemon=True).start()
    threading.Thread(target=frame_broadcaster, daemon=True).start()

    socketio.run(app, host="0.0.0.0", port=5000, debug=True, use_reloader=False)
