# ESP32-CAM Stream
- Model: ESP32-S (2AHMR-ESP32S)
- Board: AI Thinker ESP32-CAM
- Camera Module: OV2460
- Supported PixFormats: YUV422, RGB565, Grayscale
- Heap: Yes (257,712 B)
- PSRAM: Yes (4,192,124 B)
- WiFi: Yes
- Bluetooth: Yes

## PixFormat YUV422 Not Used
- Grayscale has no color information but is smaller.
- RGB565 couldn't be succesfully converted to RGB888.
- Sensor doesn't support JPEG.

## Issues with ESP32 v3.3.0
- Error: cam_hal DMA overflow
- Use ESP32 v3.2.0

## Streaming

### Over HTTP
Easy API, slow speed (1 frame / 2 seconds) with uncompressed image data. Compression requires CPU.

### Over raw UDP
Requires some work, but achieves surprisingly higher FPS. However, frame tear visible on motion (similar to rolling shutter effect).

## ToDo
- Compression of raw image data
