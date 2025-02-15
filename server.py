from flask import Flask, request
from flask_socketio import SocketIO, emit
import base64

from img_conversions import convert_to_jpeg

app = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins="*")
clients = set()


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
    image_raw = request.data
    width, height = 640, 480
    try:
        jpeg_bytes = convert_to_jpeg(image_raw, request.content_type, width, height).getvalue()
    except Exception as e:
        print('[E]', e)
        return "Internal Server Error", 555
    # Encode to base64 and send
    encoded_image = base64.b64encode(jpeg_bytes).decode('utf-8')
    if clients:
        socketio.emit('image', encoded_image)
    return "Image received", 200


@socketio.on('connect')
def handle_connect():
    clients.add(request.sid)


@socketio.on('disconnect')
def handle_disconnect():
    clients.discard(request.sid)


if __name__ == '__main__':
    socketio.run(app, host='0.0.0.0', port=5000, debug=True)
