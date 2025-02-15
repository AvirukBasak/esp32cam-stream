from flask import Flask, request
from flask_socketio import SocketIO, emit
import base64
import numpy as np
from PIL import Image
import io


app = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins="*")
clients = set()


def img_565_to_jpeg(image_raw, width, height):
    """
    Args:
    - image_raw: Raw image data from ESP32
    - width, height: Width and height of image as captured
    Returns:
    - buffer: A buffer. Use buffer.getvalue() to get binary JPEG image
    """
    array = np.frombuffer(image_raw, dtype=np.uint16).reshape((height, width))
    array = np.fliplr(array)
    r = ((array & 0xF800) >> 8).astype(np.uint8)
    g = ((array & 0x07E0) >> 3).astype(np.uint8)
    b = ((array & 0x001F) << 3).astype(np.uint8)
    rgb_array = np.stack((r, g, b), axis=-1)
    img = Image.fromarray(rgb_array, mode='RGB')
    buffer = io.BytesIO()
    img.save(buffer, format="JPEG")
    return buffer


def img_gs_to_jpeg(image_raw, width, height):
    """
    Args:
    - image_raw: Raw image data from ESP32
    - width, height: Width and height of image as captured
    Returns:
    - buffer: A buffer. Use buffer.getvalue() to get binary JPEG image
    """
    array = np.frombuffer(image_raw, dtype=np.uint8).reshape((height, width))
    array = np.fliplr(array)
    img = Image.fromarray(array, mode='L')
    buffer = io.BytesIO()
    img.save(buffer, format="JPEG")
    return buffer


@app.route('/')
def index():
    return '''
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
    '''


@app.route('/', methods=['POST'])
def upload_image():
    if request.content_type == 'application/octet-stream':
        image_raw = request.data
        width, height = 640, 480

        try:
            jpeg_bytes = img_gs_to_jpeg(image_raw, width, height).getvalue()
        except Exception as e:
            print('[E]', e)
            return "Internal Server Error", 555

        # encode to base64 and send
        encoded_image = base64.b64encode(jpeg_bytes).decode('utf-8')

        if clients:
            socketio.emit('image', encoded_image)
        return "Image received", 200
    return "Unsupported Media Type", 415


@socketio.on('connect')
def handle_connect():
    clients.add(request.sid)


@socketio.on('disconnect')
def handle_disconnect():
    clients.discard(request.sid)


if __name__ == '__main__':
    socketio.run(app, host='0.0.0.0', port=5000, debug=True)
