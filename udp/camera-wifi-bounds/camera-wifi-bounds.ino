#include <WiFi.h>
#include <Wire.h>
#include <WiFiUdp.h>

#include "esp_wifi.h"

#define CAMERA_MODEL_AI_THINKER

#include "esp_camera.h"
#include "esp_timer.h"

// Disable brownout problems
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

// ------------------------- Customizable Configurations -----------------------------

#define WIFI_SSID                      ("SamSung")
#define WIFI_PASSWD                    ("12345678")
#define WIFI_CONNECT_TIMEOUT_MS        (10000)

#define HOST_IP                        ("10.67.91.119")
#define HOST_UDP_PORT                  (8080)

#define DELAY_CAPTURE_FRAME_MS         (5)
#define DELAY_SEND_FRAME_MS            (5)
#define DELAY_INTERFRAG_MIN_MS         (0)
#define DELAY_INTERFRAG_MAX_MS         (8)

#define CAM_PIXEL_FORMAT               (PIXFORMAT_GRAYSCALE)
#define CAM_FRAMESIZE                  (FRAMESIZE_HVGA)
#define CAM_JPEG_QUALITY               (60)
#define CAM_FRAME_BUFFERS              (2)
#define CAM_XCLK_FREQ                  (20'000'000)

#define SENDER_FRAME_SEND_FAIL_THRSHLD (512)
#define SENDER_FRAG_RETRY_FLAG         (true)
#define SENDER_RETRY_FRAG_THRSHLD      (16)

// ------------------------- UDP Frame Configs: DON'T TOUCH -----------------------------

// Per-packet framing header: [frame_id:2B][frag_no:2B][total_frags:2B]
#define UDP_FRAME_HDR_SIZE (6)

// Extra metadata sent only in fragment 0: [img_width:2B][img_height:2B][pixformat:1B]
#define UDP_META_HDR_SIZE (5)

// UDP protocol constants
// WiFi sits on Ethernet (MTU=1500), minus IP(20B) + UDP(8B) = 1472B usable per datagram.
// No IP-level fragmentation this way, safe for all standard APs.
// Patch: was 1472 — lwIP's WiFiUDP TX buffer caps at 1460, using 1400 + fields
#define UDP_MTU (1400 + UDP_FRAME_HDR_SIZE + UDP_META_HDR_SIZE)  // 1411

// Usable image bytes per fragment
#define UDP_FRAG0_IMGDATA_SIZE (UDP_MTU - UDP_FRAME_HDR_SIZE - UDP_META_HDR_SIZE)  // 1400
#define UDP_FRAGN_IMGDATA_SIZE (UDP_MTU - UDP_FRAME_HDR_SIZE)                      // 1405

uint16_t Sender_FrameId = 0;
WiFiUDP Udp;

// ------------------------- UDP "Flow Control" -----------------------------

uint8_t Sender_InterFragmentDelay = DELAY_INTERFRAG_MIN_MS;
int Sender_FrameSendFailCount = 0;      // how many frames failed
int Sender_FragRetryCount = 0;          // how many times a failed current fragment retried

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
  Serial.begin(115200);
  Serial.println("[I] ESP32-CAM Image Capture and Upload");
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

inline bool init_wifi()
{
  WiFi.begin(WIFI_SSID, WIFI_PASSWD);
  bool wifi_ok = wifi_connect();
  WiFi.setSleep(false);
  return wifi_ok;
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
    s->set_hmirror(s, 0);                     // 0 = disable, 1 = enable
    s->set_vflip(s, 0);                       // 0 = disable, 1 = enable
    s->set_dcw(s, 1);                         // 0 = disable, 1 = enable
    s->set_colorbar(s, 0);                    // 0 = disable, 1 = enable
  }
}

// ------------------------- Helper Functions -----------------------------

// Write a uint16 big-endian into buf at offset, advance offset
static inline void write_u16(uint8_t *buf, size_t &frag_off, uint16_t val)
{
  buf[frag_off++] = (val >> 8) & 0xFF;
  buf[frag_off++] = val & 0xFF;
}

// Write a uint8 into buf at offset, advance offset
static inline void write_u8(uint8_t *buf, size_t &frag_off, uint8_t val)
{
  buf[frag_off++] = val;
}

static inline bool send_fragment(
  uint16_t frame_id, uint16_t frag_no,  uint16_t total_frags,
  size_t   img_len,  uint16_t img_w,    uint16_t img_h,
  uint8_t  img_fmt,  size_t   &img_off, const uint8_t *img_data
) {

  // Scratch buffer sized for one full UDP datagram
  static uint8_t Udp_Buf[UDP_MTU];
  size_t frag_off = 0;

  // --- Frame header (all fragments) ---
  // [frame_id:2B][frag_no:2B][total_frags:2B]
  write_u16(Udp_Buf, frag_off, frame_id);
  write_u16(Udp_Buf, frag_off, frag_no);
  write_u16(Udp_Buf, frag_off, total_frags);

  // --- Metadata header (fragment 0 only) ---
  // [img_width:2B][img_height:2B][pixformat:1B]
  if (frag_no == 0) {
    write_u16(Udp_Buf, frag_off, img_w);
    write_u16(Udp_Buf, frag_off, img_h);
    write_u8 (Udp_Buf, frag_off, img_fmt);
  }

  // --- Image data payload ---
  size_t data_capacity = UDP_MTU - frag_off;
  size_t data_len      = min(data_capacity, img_len - img_off);

  memcpy(Udp_Buf + frag_off, img_data + img_off, data_len);
  img_off  += data_len;
  frag_off += data_len;

  // Send the datagram
  Udp.beginPacket(HOST_IP, HOST_UDP_PORT);
  size_t sent = Udp.write(Udp_Buf, frag_off);

  bool udp_ok = Udp.endPacket();
  return udp_ok && (sent == frag_off);
}

static inline bool send_frame(camera_fb_t *fb)
{
  bool return_val = false;

  const uint8_t *img_data = fb->buf;

  size_t   img_len = fb->len;
  uint16_t img_w   = (uint16_t) fb->width;
  uint16_t img_h   = (uint16_t) fb->height;
  uint8_t  img_fmt = (uint8_t)  fb->format; // maps directly to pixformat_t enum

  // Calculate total fragments needed
  uint16_t total_frags;
  if (img_len <= UDP_FRAG0_IMGDATA_SIZE) {
    total_frags = 1;
  } else {
    size_t remaining = img_len - UDP_FRAG0_IMGDATA_SIZE;
    // The following is basically taking the ceiling value
    total_frags = 1 + (uint16_t) ((remaining + UDP_FRAGN_IMGDATA_SIZE - 1) / UDP_FRAGN_IMGDATA_SIZE);
  }

  size_t img_off = 0;

  uint16_t frag_no = 0;
  for (frag_no = 0; frag_no < total_frags; ++frag_no) {
    size_t img_off_before = img_off; // snapshot
    bool frag_ok = false;

send_frame_retry_fragment:

    if (Sender_InterFragmentDelay) delay(Sender_InterFragmentDelay);

    frag_ok = send_fragment(
      Sender_FrameId, frag_no, total_frags,
      img_len,  img_w,   img_h,
      img_fmt,  img_off, img_data
    );

    // taskYIELD(); // just yield to lwIP task, no fixed sleep
    // delay(1);    // instead use non-blocking sleep

    // On success, next iteration
    if (frag_ok) {
      // On each success, reduce interframe delay slower
      Sender_InterFragmentDelay = (uint8_t) MAX(
        (int16_t) Sender_InterFragmentDelay - 2,
        DELAY_INTERFRAG_MIN_MS
      );
      // After successful send, reset retry counter
      Sender_FragRetryCount = 0;
      goto send_frame_next_fragment;
    }

    // On failure, backoff interframe delay faster
    Sender_InterFragmentDelay = (uint8_t) MIN(
      (int16_t) Sender_InterFragmentDelay + 4,
      DELAY_INTERFRAG_MAX_MS
    );

    // On failure, attempt retry if enabled
    if (SENDER_FRAG_RETRY_FLAG) {

      Sender_FragRetryCount += 1;

      if (Sender_FragRetryCount > SENDER_RETRY_FRAG_THRSHLD) {
        // Stop retrying on threshold, skip frame
        return_val = false;
        goto send_frame_next_frame;
      } else {
        // Retry fragment otherwise
        img_off = img_off_before; // roll back
        goto send_frame_retry_fragment;
      }

    } else {
      // If retry disabled, just skip frame
      return_val = false;
      goto send_frame_next_frame;
    }


send_frame_next_fragment:

  }

  // Only time its true is if all fragments were sent
  return_val = (frag_no == total_frags);


send_frame_next_frame:
  // Wraps at 65535 → 0, receiver handles it
  // Serial.printf("[I] send_frame(%d): frags: %d/%d, success?: %d\n", Frame_Id, frag_no, total_frags, return_val);
  Sender_FrameId++;
  return return_val;
}

// ------------------------- Main Cam Capture Logic -----------------------------

inline bool capture_n_upload()
{
  if (!wifi_connect(true)) return false;

  // Take a picture
  if (DELAY_CAPTURE_FRAME_MS) delay(DELAY_CAPTURE_FRAME_MS);
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[E] Camera capture failed");
    return false;
  }

  // Send the frame
  if (DELAY_SEND_FRAME_MS) delay(DELAY_SEND_FRAME_MS);
  bool frame_ok = send_frame(fb);
  // Return the frame buffer to be reused
  esp_camera_fb_return(fb);

  if (!frame_ok) {

    Sender_FrameSendFailCount += 1;

    if (SENDER_FRAME_SEND_FAIL_THRSHLD > 0) {
      // If threshold is enabled
      if (Sender_FrameSendFailCount > SENDER_FRAME_SEND_FAIL_THRSHLD) {
          Serial.printf("[E] Failed %d consecutive frame send, stopping captures\n", SENDER_FRAME_SEND_FAIL_THRSHLD);
          // Stop captures once faiure threshold crossed
          return false;
      }
      // Else print an error but continue capturing
      Serial.printf("[E] Frame (%d) send failed\n", Sender_FrameId);

    }

    // Continue capturing if threshold is not enabled
    return true;
  }

  // Occasional frame send failures are expected, so only track consecutive failures
  Sender_FrameSendFailCount = 0;
  return true;
}

void setup()
{
  init_board();
  init_serial();
  bool wifi_ok = init_wifi();
  if (wifi_ok) {
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
    delay(1);
  } else {
    Serial.printf("Seconds Elapsed Counter = %d\n", ++dummy_counter);
    delay(1000);
  }

}
