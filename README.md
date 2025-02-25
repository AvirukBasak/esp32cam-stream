# ESP32-CAM Stream
- Model: ESP32-S (2AHMR-ESP32S)
- Camera Module: OV2460
- Supported PixFormats: YUV422, RGB565, Grayscale
- PSRAM: No
- WiFi: Yes
- Bluetooth: Yes

## PixFormat YUV422 Used
Grayscale has no color information. RGB565 couldn't be succesfully converted to RGB888. Can't use JPEG coz no PSRAM for conversion.

## Streaming

### Over HTTP
Easy API, slow speed (1 frame / 2 seconds) with uncompressed image data. Compression requires CPU.

### Over raw UDB
Untested

## ToDo
- Compression of YUV422 data.
- Transfer over raw UDP
- Recovery of frame from UDP packets.
