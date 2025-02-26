#include <WiFi.h>
#include <Wire.h>
#include <HTTPClient.h>
#include "miniz.h"
#include "esp_camera.h"
#include "esp_timer.h"

// Disable brownout problems
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#define CAMERA_MODEL_AI_THINKER
#define CAMERA_PIXEL_FORMAT (PIXFORMAT_YUV422)
#define CAMERA_FRAMESIZE (FRAMESIZE_SVGA)
#define CAPTURE_N_UPLOAD_DELAY_MS (5000)

#define WIFI_SSID ("Begonia")
#define WIFI_PASSWD ("a r s h o l a")

#define SERVER_IP ("192.168.231.119")
#define SERVER_UDP_PORT (8080)
#define SERVER_UDP_PAYLOAD_SIZE (1024)
#define SERVER_HTTP_URL ("http://192.168.231.119:5000")

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

const char *HTTP_ContentType = NULL;
const char *HTTP_ImageWidth = NULL;
const char *HTTP_ImageHeight = NULL;

int Server_ErrCount = 0;
int Server_ErrCountThreshold = 5;

inline void init_first() {
  setCpuFrequencyMhz(240);
  // Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
}

inline void init_serial() {
  Serial.begin(115200);
  Serial.println("ESP32-CAM Image Capture and Upload");
}

inline void init_wifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWD);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.printf(" Connected (%s)\n", WiFi.localIP().toString().c_str());
  WiFi.setSleep(false);
}

inline void init_cam() {

#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = CAMERA_PIXEL_FORMAT;

  // Lower resolution for less bandwidth usage and faster upload
  config.frame_size = CAMERA_FRAMESIZE;  // Frame Size
  config.jpeg_quality = 60;              // 0-63, lower is higher quality
  config.fb_count = 1;

  // Deep slled ESP on camera init failure
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[E] Camera init failed with error 0x%x\n", err);
    esp_deep_sleep_start();
    return;
  }

  Serial.println("Camera initialized successfully");
}

inline void config_camsensor() {
  // Set higher quality after initialization if you want
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 0);                  // -2 to 2
    s->set_contrast(s, 0);                    // -2 to 2
    s->set_saturation(s, 0);                  // -2 to 2
    s->set_special_effect(s, 0);              // 0 = no effect
    s->set_whitebal(s, 1);                    // 0 = disable, 1 = enable
    s->set_awb_gain(s, 1);                    // 0 = disable, 1 = enable
    s->set_wb_mode(s, 0);                     // Auto mode
    s->set_exposure_ctrl(s, 1);               // 0 = disable, 1 = enable
    s->set_gain_ctrl(s, 1);                   // 0 = disable, 1 = enable
    s->set_aec2(s, 0);                        // 0 = disable, 1 = enable
    s->set_ae_level(s, 0);                    // -2 to 2
    s->set_aec_value(s, 300);                 // 0 to 1200
    s->set_gain_ctrl(s, 1);                   // 0 = disable, 1 = enable
    s->set_agc_gain(s, 0);                    // 0 to 30
    s->set_gainceiling(s, (gainceiling_t)0);  // 0 to 6
    s->set_bpc(s, 0);                         // 0 = disable, 1 = enable
    s->set_wpc(s, 1);                         // 0 = disable, 1 = enable
    s->set_raw_gma(s, 1);                     // 0 = disable, 1 = enable
    s->set_lenc(s, 1);                        // 0 = disable, 1 = enable
    s->set_hmirror(s, 0);                     // 0 = disable, 1 = enable
    s->set_vflip(s, 0);                       // 0 = disable, 1 = enable
    s->set_dcw(s, 1);                         // 0 = disable, 1 = enable
    s->set_colorbar(s, 0);                    // 0 = disable, 1 = enable
  }
}

inline void init_httpConfig() {
  switch (CAMERA_PIXEL_FORMAT) {
    case PIXFORMAT_YUV422:
      HTTP_ContentType = ImageUploadFormats::YUV422;
      break;
    case PIXFORMAT_GRAYSCALE:
      HTTP_ContentType = ImageUploadFormats::GS;
      break;
    case PIXFORMAT_RGB565:
      HTTP_ContentType = ImageUploadFormats::RGB565;
      break;
    case PIXFORMAT_RGB444:
      HTTP_ContentType = ImageUploadFormats::RGB444;
      break;
    case PIXFORMAT_RGB555:
      HTTP_ContentType = ImageUploadFormats::RGB555;
      break;
    case PIXFORMAT_RGB888:
      HTTP_ContentType = ImageUploadFormats::RGB888;
      break;
    case PIXFORMAT_JPEG:
      HTTP_ContentType = ImageUploadFormats::JPEG;
      break;
    case PIXFORMAT_YUV420:
      HTTP_ContentType = ImageUploadFormats::YUV420;
      break;
    case PIXFORMAT_RAW:
      HTTP_ContentType = ImageUploadFormats::RAW;
      break;
  }

  switch (CAMERA_FRAMESIZE) {
    case FRAMESIZE_QQVGA:
      HTTP_ImageWidth = "160";
      HTTP_ImageHeight = "120";
      break;
    case FRAMESIZE_QVGA:
      HTTP_ImageWidth = "320";
      HTTP_ImageHeight = "240";
      break;
    case FRAMESIZE_CIF:
      HTTP_ImageWidth = "400";
      HTTP_ImageHeight = "296";
      break;
    case FRAMESIZE_VGA:
      HTTP_ImageWidth = "640";
      HTTP_ImageHeight = "480";
      break;
    case FRAMESIZE_SVGA:
      HTTP_ImageWidth = "800";
      HTTP_ImageHeight = "600";
      break;
    case FRAMESIZE_XGA:
      HTTP_ImageWidth = "1024";
      HTTP_ImageHeight = "768";
      break;
    case FRAMESIZE_HD:
      HTTP_ImageWidth = "1280";
      HTTP_ImageHeight = "720";
      break;
    case FRAMESIZE_SXGA:
      HTTP_ImageWidth = "1280";
      HTTP_ImageHeight = "1024";
      break;
    case FRAMESIZE_UXGA:
      HTTP_ImageWidth = "1600";
      HTTP_ImageHeight = "1200";
      break;
    case FRAMESIZE_FHD:
      HTTP_ImageWidth = "1920";
      HTTP_ImageHeight = "1080";
      break;
    case FRAMESIZE_QXGA:
      HTTP_ImageWidth = "2048";
      HTTP_ImageHeight = "1536";
      break;
    case FRAMESIZE_WQXGA:
      HTTP_ImageWidth = "2560";
      HTTP_ImageHeight = "1600";
      break;
    case FRAMESIZE_INVALID:
      Serial.println("[E] Invalid FRAMESIZE");
      esp_deep_sleep_start();
      break;
    default:
      Serial.println("[E] Don't care about other frame sizes");
      break;
  }
}

inline void capture_n_upload() {
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("[E] Lost WiFi connection, connecting...");
    while (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      delay(1000);
    }
    Serial.printf(" Connected (%s)\n", WiFi.localIP().toString().c_str());
  }

  // Take a picture
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[E] Camera capture failed");
    return;
  }

  HTTPClient http;
  http.begin(SERVER_HTTP_URL);
  http.addHeader("Content-Type", HTTP_ContentType);
  http.addHeader("X-Image-Width", HTTP_ImageWidth);
  http.addHeader("X-Image-Height", HTTP_ImageHeight);

  size_t compressed_size = 0;
  uint8_t *compressed_data = (uint8_t) tdefl_compress_buffer(tdefl_compressor *d, const void *pIn_buf, size_t in_buf_size, tdefl_flush flush)
  if (!compressed_data) {
    Serial.println("Compression failed!");
    esp_deep_sleep_start();
    return;
  }

  int httpResponseCode = http.POST(compressed_data, compressed_size);
  if (httpResponseCode > 0)
    ;
  else {
    Server_ErrCount += 1;
    if (Server_ErrCount > Server_ErrCountThreshold) {
      Serial.printf("[E] Server error count exceeded threshold (%d)\n", Server_ErrCountThreshold);
      esp_deep_sleep_start();
    }
    Serial.printf("[E] Error on HTTP request: %s (%d)\n", http.errorToString(httpResponseCode).c_str(), httpResponseCode);
  }

  http.end();
  mz_free(compressed_data);

  // Return the frame buffer to be reused
  esp_camera_fb_return(fb);
}

void setup() {
  init_first();
  init_serial();
  init_wifi();
  init_cam();
  config_camsensor();
  init_httpConfig();
}

void loop() {
  capture_n_upload();
  delay(CAPTURE_N_UPLOAD_DELAY_MS);
}
