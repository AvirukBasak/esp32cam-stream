#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include "esp_timer.h"
#include "soc/soc.h"           // Disable brownout problems
#include "soc/rtc_cntl_reg.h"  // Disable brownout problems

// Select camera model - uncomment the one you're using
//#define CAMERA_MODEL_WROVER_KIT
//#define CAMERA_MODEL_ESP_EYE
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_V2_PSRAM
#define CAMERA_MODEL_AI_THINKER // ESP32-CAM

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// WiFi credentials
const char* ssid = "Begonia";
const char* password = "a r s h o l a";

// The URL where we will send the image
const char* serverUrl = "http://192.168.181.119:5000";

void setup() {
  // Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  
  Serial.begin(115200);
  Serial.println("ESP32-CAM Image Capture and Upload");
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected to WiFi, IP address: ");
  Serial.println(WiFi.localIP());
  
  // Configure camera
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
  config.pixel_format = PIXFORMAT_RGB565;
  
  // Start with lower resolution for less bandwidth usage and faster upload
  config.frame_size = FRAMESIZE_VGA;  // 640x480
  config.jpeg_quality = 12;           // 0-63, lower is higher quality
  config.fb_count = 1;
  
  // Camera initialization with enhanced error handling
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    delay(5000);
    ESP.restart();
    return;
  }
  
  Serial.println("Camera initialized successfully");
  
  // Set higher quality after initialization if you want
  sensor_t * s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 0);      // -2 to 2
    s->set_contrast(s, 0);        // -2 to 2
    s->set_saturation(s, 0);      // -2 to 2
    s->set_special_effect(s, 0);  // 0 = no effect
    s->set_whitebal(s, 1);        // 0 = disable, 1 = enable
    s->set_awb_gain(s, 1);        // 0 = disable, 1 = enable
    s->set_wb_mode(s, 0);         // Auto mode
    s->set_exposure_ctrl(s, 1);   // 0 = disable, 1 = enable
    s->set_gain_ctrl(s, 1);       // 0 = disable, 1 = enable
    s->set_aec2(s, 0);            // 0 = disable, 1 = enable
    s->set_ae_level(s, 0);        // -2 to 2
    s->set_aec_value(s, 300);     // 0 to 1200
    s->set_gain_ctrl(s, 1);       // 0 = disable, 1 = enable
    s->set_agc_gain(s, 0);        // 0 to 30
    s->set_gainceiling(s, (gainceiling_t)0);  // 0 to 6
    s->set_bpc(s, 0);             // 0 = disable, 1 = enable
    s->set_wpc(s, 1);             // 0 = disable, 1 = enable
    s->set_raw_gma(s, 1);         // 0 = disable, 1 = enable
    s->set_lenc(s, 1);            // 0 = disable, 1 = enable
    s->set_hmirror(s, 0);         // 0 = disable, 1 = enable
    s->set_vflip(s, 0);           // 0 = disable, 1 = enable
    s->set_dcw(s, 1);             // 0 = disable, 1 = enable
    s->set_colorbar(s, 0);        // 0 = disable, 1 = enable
  }
}

void loop() {
  captureAndUploadImage();
  delay(5000);
}

void captureAndUploadImage() {
  // Take a picture
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }
  
  Serial.printf("Captured image: %dx%d, size: %d bytes\n", fb->width, fb->height, fb->len);
  
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    Serial.println("Starting HTTP POST request...");
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/octet-stream");
    
    // Send HTTP POST request with the image data
    int httpResponseCode = http.POST(fb->buf, fb->len);
    
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println(httpResponseCode);
      Serial.println(response);
    } else {
      Serial.printf("Error on HTTP request: %s (%d)\n", http.errorToString(httpResponseCode).c_str(), httpResponseCode);
    }
    
    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
  
  // Return the frame buffer to be reused
  esp_camera_fb_return(fb);
}
