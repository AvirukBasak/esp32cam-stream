#include <WiFi.h>
#include <Wire.h>

#include "esp_wifi.h"

#define CAMERA_MODEL_AI_THINKER

#include "esp_camera.h"
#include "esp_timer.h"

// Disable brownout problems
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ------------------------- Customizable Configurations -----------------------------

#define WIFI_CONNECT_TIMEOUT_MS        (10000)

#define DELAY_CAPTURE_FRAME_MS         (5)
#define DELAY_SEND_FRAME_MS            (5)

#define CAM_PIXEL_FORMAT               (PIXFORMAT_GRAYSCALE)
#define CAM_FRAMESIZE                  (FRAMESIZE_HVGA)
#define CAM_JPEG_QUALITY               (60)
#define CAM_FRAME_BUFFERS              (3)
#define CAM_XCLK_FREQ                  (20'000'000)

// ---------------------------------- Input By User -------------------------------------

static String WiFi_SSID;
static String WiFi_Passwd;

static String Host_IP;
static int    Host_Port = 8080;

// ------------------------- TCP Configs: DON'T TOUCH -----------------------------

// [frame_id:2B][img_len:4B][img_w:2B][img_h:2B][pixfmt:1B]
#define TCP_IMGFRAME_HDR_SIZE  (11)
#define TCP_MAX_CHUNK_SIZE     (1500)
#define TCP_CONNECT_TIMEOUT_MS (10000)

#define START_FRAME_NO         ((0xFFFF) - 50)

// This wraps around on overflow automatically
uint16_t Sender_FrameId = START_FRAME_NO;

WiFiClient Tcp;

// --------------------------- Init Procedures ------------------------------

static bool enable_camera = false; // true is capture mode

inline void init_board()
{
  setCpuFrequencyMhz(240);
  // Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
}

inline void init_serial()
{
  Serial.begin(SERIAL_BAUD_RATE);
  Serial.printf("[I] ESP32-CAM Image Capture and Upload (baud %d)\n", SERIAL_BAUD_RATE);
}

bool wifi_connect(bool reconnect = false) {
  if (WiFi.status() == WL_CONNECTED) return true;

  if (reconnect) Serial.print("[E] Lost WiFi, reconnecting...");
  else           Serial.print("[I] Conntecting to WiFi...");

  uint32_t deadline = millis() + WIFI_CONNECT_TIMEOUT_MS;
  bool connected = false;

  while (millis() < deadline) {
    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      break;
    }
    Serial.print(".");
    delay(1000);
  }

  if (!connected) {
    Serial.println("\n[E] WiFi connect timed out");
    return false; // dummy mode
  }

  Serial.printf(" Connected (%s)\n", WiFi.localIP().toString().c_str());
  return true;
}

bool tcp_connect(bool reconnect = false) {
  if (Tcp.connected()) return true;

  if (reconnect) Serial.print ("[E] Lost TCP, reconnecting...");
  else           Serial.printf("[I] Connecting TCP to %s:%d...", HOST_IP, HOST_TCP_PORT);

  uint32_t deadline = millis() + TCP_CONNECT_TIMEOUT_MS;
  bool connected = false;

  while (millis() < deadline) {
    if (Tcp.connect(HOST_IP, HOST_TCP_PORT)) {
      connected = true;
      break;
    }
    Serial.print(".");
    delay(1000);
  }

  if (!connected) {
    Serial.println("\n[E] TCP connect timed out");
    return false; // dummy mode
  }

  Serial.println(" TCP connected");
  return true;
}

inline bool init_wifi()
{
  WiFi.begin(WIFI_SSID, WIFI_PASSWD);
  bool wifi_ok = wifi_connect();
  WiFi.setSleep(false);
  return wifi_ok;
}

inline bool init_tcp(bool wifi_ok) {
  if (!wifi_ok) {
    Serial.println("[E] No WiFi, cannot create TCP connection");
    return false;
  }
  return tcp_connect();
}

inline void init_cam()
{
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

  // Deep sleep ESP on camera init failure
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[E] Camera init failed with error 0x%x\n", err);
    esp_deep_sleep_start();
    return;
  }

  Serial.println("[I] Camera initialized successfully");
}

inline void config_cam()
{
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
    s->set_vflip(s, 0);                       // 0 = disable, 1 = enable
    s->set_hmirror(s, 1);                     // 0 = disable, 1 = enable
    {
      // Horizontal mirror - direct register write (bypasses the buggy abstraction)
      s->set_reg(s, 0xFF, 0xFF, 0x01);  // switch to sensor register bank
      s->set_reg(s, 0x04, 0x80, 0x80);  // set mirror bit
      // Horizontal mirror - direct register write (alternative method)
      // s->set_reg(s, 0xFF, 0xFF, 0x01);         // sensor bank
      // uint8_t val = s->get_reg(s, 0x04, 0xFF); // read current value of reg 0x04
      // s->set_reg(s, 0x04, 0xFF, val | 0x80);   // set bit 7 (mirror)
    }
    s->set_dcw(s, 1);                         // 0 = disable, 1 = enable
    s->set_colorbar(s, 0);                    // 0 = disable, 1 = enable
  }
}

// ------------------------- Helper Functions -----------------------------

// Write a uint16 big-endian into buf at offset, advance offset
static inline void write_u16(uint8_t *buf, size_t &off, uint16_t val)
{
  buf[off++] = (val >> 8) & 0xFF;
  buf[off++] = val & 0xFF;
}

// Write a uint8 into buf at offset, advance offset
static inline void write_u8(uint8_t *buf, size_t &off, uint8_t val)
{
  buf[off++] = val;
}

static inline bool send_frame(camera_fb_t *fb)
{
  if (!Tcp.connected()) return false;

  uint8_t Tcp_Hdr[TCP_IMGFRAME_HDR_SIZE];

  // Build header
  size_t off = 0;
  write_u16(Tcp_Hdr, off, Sender_FrameId);              // 2B
  write_u16(Tcp_Hdr, off, (uint32_t) fb->len);    // 4B
  write_u16(Tcp_Hdr, off, (uint16_t) fb->width);  // 2B
  write_u16(Tcp_Hdr, off, (uint16_t) fb->height); // 2B
  write_u8 (Tcp_Hdr, off, (uint8_t)  fb->format); // 1B

  // Send header
  if (Tcp.write(Tcp_Hdr, TCP_IMGFRAME_HDR_SIZE) != TCP_IMGFRAME_HDR_SIZE) {
    Sender_FrameId++;
    return false;
  }

  // Send image data in lwIP-safe chunks
  const uint8_t *ptr = fb->buf;
  size_t remaining   = fb->len;

  while (remaining > 0) {
    size_t write_size = min(remaining, (size_t) TCP_MAX_CHUNK_SIZE);
    size_t sent  = Tcp.write(ptr, write_size);
    if (sent == 0) {
      Sender_FrameId++;
      return false;
    }
    ptr       += sent;
    remaining -= sent;
  }

  Sender_FrameId++;
  return true;
}

// ------------------------- Main Cam Capture Logic -----------------------------

inline bool capture_n_upload()
{
  if (!wifi_connect(true)) return false;
  if (!tcp_connect(true))  return false;

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[E] Camera capture failed");
    return true;
  }

  bool frame_ok = send_frame(fb);
  esp_camera_fb_return(fb);

  if (!frame_ok) {
    Serial.printf("[E] Frame %d send failed, dropping TCP connection\n", Sender_FrameId);
    Tcp.stop(); // reconnect attempt on next iteration, timeout governs dummy fallback
  }

  return true;
}

void setup()
{
  init_board();
  init_serial();
  bool wifi_ok = init_wifi();
  bool tcp_ok  = init_tcp(wifi_ok);
  if (wifi_ok && tcp_ok) {
    enable_camera = true;
    // configure camera
    init_cam();
    config_cam();
  } else {
    enable_camera = false;
  }
}

void loop()
{
  static uint64_t dummy_counter = 0;

  if (enable_camera) {
    esp_wifi_set_ps(WIFI_PS_NONE);
    enable_camera = capture_n_upload();
    delay(CAPTURE_N_UPLOAD_DELAY_MS);
  } else {
    Serial.printf("Seconds Elapsed Counter = %d\n", ++dummy_counter);
    delay(1000);
  }

}
