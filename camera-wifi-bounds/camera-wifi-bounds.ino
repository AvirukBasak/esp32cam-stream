#include <WiFi.h>
#include <Wire.h>
#include <HTTPClient.h>

#define CAMERA_MODEL_AI_THINKER

#include "esp_camera.h"
#include "esp_timer.h"

// Disable brownout problems
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#define WIFI_SSID                  ("Begonia")
#define WIFI_PASSWD                ("a r s h o l a")
#define CAM_PIXEL_FORMAT           (PIXFORMAT_YUV422)
#define CAM_FRAMESIZE              (FRAMESIZE_SVGA)
#define CAM_JPEG_QUALITY           (60)
#define CAM_FRAME_BUFFERS          (3)
#define CAM_XCLK_FREQ              (20'000'000)

#define CAPTURE_N_UPLOAD_DELAY_MS  (500)

#define HTTP_CONTENT_TYPE          ("image/yuv422")
#define HTTP_IMG_WIDTH             ("800")
#define HTTP_IMG_HEIGHT            ("600")

#define SERVER_ERR_COUNT_THRSHLD   (5)
#define SERVER_UDP_PAYLOAD_SIZE    (1024)
#define SERVER_IP                  ("192.168.231.119")
#define SERVER_UDP_PORT            (8080)
#define SERVER_HTTP_URL            ("http://192.168.121.119:5000")

int Server_ErrCount = 0;

inline void init_board() {
  setCpuFrequencyMhz(240);
  // Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
}

inline void init_serial() {
  Serial.begin(115200);
  Serial.println("[I] ESP32-CAM Image Capture and Upload");
}

inline void init_wifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWD);
  Serial.print("[I] Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.printf(" Connected (%s)\n", WiFi.localIP().toString().c_str());
  WiFi.setSleep(false);
}

inline void init_cam() {

#define PWDN_GPIO_NUM  (32)
#define RESET_GPIO_NUM (-1)
#define XCLK_GPIO_NUM  (0)
#define SIOD_GPIO_NUM  (26)
#define SIOC_GPIO_NUM  (27)
#define Y9_GPIO_NUM    (35)
#define Y8_GPIO_NUM    (34)
#define Y7_GPIO_NUM    (39)
#define Y6_GPIO_NUM    (36)
#define Y5_GPIO_NUM    (21)
#define Y4_GPIO_NUM    (19)
#define Y3_GPIO_NUM    (18)
#define Y2_GPIO_NUM    (5)
#define VSYNC_GPIO_NUM (25)
#define HREF_GPIO_NUM  (23)
#define PCLK_GPIO_NUM  (22)

  Serial.printf("[I] Free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("[I] Free PSRAM: %d bytes\n", ESP.getFreePsram());

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = CAM_XCLK_FREQ;
  config.pixel_format = CAM_PIXEL_FORMAT;
  config.frame_size   = CAM_FRAMESIZE;
  config.jpeg_quality = CAM_JPEG_QUALITY;
  config.fb_count     = CAM_FRAME_BUFFERS;
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;

  // Deep slled ESP on camera init failure
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[E] Camera init failed with error 0x%x\n", err);
    esp_deep_sleep_start();
    return;
  }

  Serial.println("Camera initialized successfully");
}

inline void config_cam() {
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
  http.addHeader("Content-Type",   HTTP_CONTENT_TYPE);
  http.addHeader("X-Image-Width",  HTTP_IMG_WIDTH);
  http.addHeader("X-Image-Height", HTTP_IMG_HEIGHT);

  int httpResponseCode = http.POST(fb->buf, fb->len);
  if (httpResponseCode > 0)
    ;
  else {
    Server_ErrCount += 1;
    if (Server_ErrCount > SERVER_ERR_COUNT_THRSHLD) {
      Serial.printf("[E] Server error count exceeded threshold (%d)\n", SERVER_ERR_COUNT_THRSHLD);
      esp_deep_sleep_start();
    }
    Serial.printf("[E] Error on HTTP request: %s (%d)\n", http.errorToString(httpResponseCode).c_str(), httpResponseCode);
  }

  http.end();

  // Return the frame buffer to be reused
  esp_camera_fb_return(fb);
}

void setup() {
  init_board();
  init_serial();
  init_wifi();
  init_cam();
  config_cam();
}

void loop() {
  capture_n_upload();
  delay(CAPTURE_N_UPLOAD_DELAY_MS);
}
