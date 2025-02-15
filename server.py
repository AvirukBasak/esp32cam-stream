from flask import Flask, request
from flask_socketio import SocketIO, emit
import base64

from img_conversions import img_565_to_jpeg, img_555_to_jpeg, img_444_to_jpeg, img_gs_to_jpeg

app = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins="*")
clients = set()


"""
struct ImageUploadFormats {
  static constexpr const char *RGB565 = "image/rgb565";
  static constexpr const char *GS = "image/grayscale";
  static constexpr const char *RGB444 = "image/rgb444";
  static constexpr const char *RGB555 = "image/rgb555";
  static constexpr const char *RGB888 = "image/rgb888";
  static constexpr const char *JPEG = "image/jpeg";
  static constexpr const char *YUV422 = "image/yuv422";
  static constexpr const char *YUV420 = "image/yuv420";
  static constexpr const char *RAW = "image/raw";
};
"""

ImageUploadFormats = {
    "image/rgb565": "img_565_to_jpeg",
    "image/grayscale": "img_gs_to_jpeg",
    "image/rgb444": "img_444_to_jpeg",
    "image/rgb555": "img_555_to_jpeg",
    "image/rgb888": "img_888_to_jpeg",
    "image/jpeg": "img_jpeg_to_jpeg",
    "image/yuv422": "img_yuv422_to_jpeg",
    "image/yuv420": "img_yuv420_to_jpeg",
    "image/raw": "img_raw_to_jpeg"
}


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
    if request.content_type in ImageUploadFormats:
        image_raw = request.data
        width, height = 640, 480

        try:
            # Dynamically call the appropriate conversion function
            convert_function = globals()[ImageUploadFormats[request.content_type]]
            jpeg_bytes = convert_function(image_raw, width, height).getvalue()
        except Exception as e:
            print('[E]', e)
            return "Internal Server Error", 555

        # Encode to base64 and send
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
