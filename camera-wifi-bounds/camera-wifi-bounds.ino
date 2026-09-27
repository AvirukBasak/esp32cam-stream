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

// ------------------------- Customizable Configurations -----------------------------

#define WIFI_SSID                      ("SamSung")
#define WIFI_PASSWD                    ("12345678")
#define HOST_IP                        ("10.130.207.119")
#define HOST_UDP_PORT                  (8080)

#define CAPTURE_N_UPLOAD_DELAY_MS      (50)

#define CAM_PIXEL_FORMAT               (PIXFORMAT_GRAYSCALE)
#define CAM_FRAMESIZE                  (FRAMESIZE_HVGA)
#define CAM_JPEG_QUALITY               (60)
#define CAM_FRAME_BUFFERS              (2)
#define CAM_XCLK_FREQ                  (20'000'000)

#define SENDER_FRAME_SEND_FAIL_THRSHLD (512)
#define SENDER_RETRY_FRAG_THRSHLD      (16)
#define SENDER_FRAG_RETRY_FLAG         (true)

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
#define UDP_FRAG0_DATA_SIZE (UDP_MTU - UDP_FRAME_HDR_SIZE - UDP_META_HDR_SIZE)  // 1400
#define UDP_FRAGN_DATA_SIZE (UDP_MTU - UDP_FRAME_HDR_SIZE)                      // 1405

uint16_t Frame_Id = 0;
WiFiUDP Udp;

// Scratch buffer sized for one full UDP datagram
static uint8_t Udp_Buf[UDP_MTU];

// ------------------------- UDP "Flow Control" -----------------------------

int Sender_FrameSendFailCount = 0;  // how many frames failed
int Sender_FragRetryCount = 0;      // how many times a failed current fragment retried

// --------------------------- Init Procedures ------------------------------

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

inline void init_wifi()
{
  WiFi.begin(WIFI_SSID, WIFI_PASSWD);
  Serial.print("[I] Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.printf(" Connected (%s)\n", WiFi.localIP().toString().c_str());
  WiFi.setSleep(false);
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

static inline bool send_fragment(
  uint16_t frame_id, uint16_t frag_no,     uint16_t total_frags,
  size_t   img_len,  uint16_t img_w,       uint16_t img_h,
  uint8_t  img_fmt,  size_t   &img_offset, size_t   &pkt_off,
  size_t   &sent,    const uint8_t *img_data
) {
  // --- Frame header (all fragments) ---
  // [frame_id:2B][frag_no:2B][total_frags:2B]
  write_u16(Udp_Buf, pkt_off, frame_id);
  write_u16(Udp_Buf, pkt_off, frag_no);
  write_u16(Udp_Buf, pkt_off, total_frags);

  // --- Metadata header (fragment 0 only) ---
  // [img_width:2B][img_height:2B][pixformat:1B]
  if (frag_no == 0) {
    write_u16(Udp_Buf, pkt_off, img_w);
    write_u16(Udp_Buf, pkt_off, img_h);
    write_u8 (Udp_Buf, pkt_off, img_fmt);
  }

  // --- Image data payload ---
  size_t data_capacity = UDP_MTU - pkt_off;
  size_t data_len      = min(data_capacity, img_len - img_offset);

  memcpy(Udp_Buf + pkt_off, img_data + img_offset, data_len);
  img_offset += data_len;
  pkt_off    += data_len;

  // Send the datagram
  Udp.beginPacket(HOST_IP, HOST_UDP_PORT);
  sent = Udp.write(Udp_Buf, pkt_off);

  bool ok = Udp.endPacket();
  return ok;
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
  if (img_len <= UDP_FRAG0_DATA_SIZE) {
    total_frags = 1;
  } else {
    size_t remaining = img_len - UDP_FRAG0_DATA_SIZE;
    total_frags = 1 + (uint16_t)((remaining + UDP_FRAGN_DATA_SIZE - 1) / UDP_FRAGN_DATA_SIZE);
  }

  size_t img_offset = 0;

  uint16_t frag_no = 0;
  for (frag_no = 0; frag_no < total_frags; ++frag_no) {

send_frame_retry_fragment:

    size_t pkt_off = 0;
    size_t sent    = 0;

    bool frag_ok = send_fragment(
      Frame_Id, frag_no,    total_frags,
      img_len,  img_w,      img_h,
      img_fmt,  img_offset, pkt_off,
      sent,     img_data
    );

    // taskYIELD(); // just yield to lwIP task, no fixed sleep
    // delay(1);    // instead use non-blocking sleep

    // On success, next iteration
    if (frag_ok && sent == pkt_off) {
      // After successful send, reset retry counter
      Sender_FragRetryCount = 0;
      goto send_frame_next_fragment;
    }

    // On failure, attempt retry if enabled
    if (SENDER_FRAG_RETRY_FLAG) {

      Sender_FragRetryCount += 1;

      if (Sender_FragRetryCount > SENDER_RETRY_FRAG_THRSHLD) {
        // Stop retrying on threshold, skip frame
        return_val = false;
        goto send_frame_next_frame;
      } else {
        // Retry fragment otherwise
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
  Frame_Id++;
  return return_val;
}

// ------------------------- Main Cam Capture Logic -----------------------------

inline bool capture_n_upload()
{
  bool return_val = false;
  bool frame_ok   = false;

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
    goto capture_n_upload_clean_return;
  }

  // Send the frame
  frame_ok = send_frame(fb);

  if (!frame_ok) {

    Sender_FrameSendFailCount += 1;

    if (SENDER_FRAME_SEND_FAIL_THRSHLD > 0) {

      if (Sender_FrameSendFailCount > SENDER_FRAME_SEND_FAIL_THRSHLD) {
          Serial.printf("[E] Failed %d consecutive frame send, stopping captures\n", SENDER_FRAME_SEND_FAIL_THRSHLD);
          // Stop captures and upload
          return_val = false;
          goto capture_n_upload_clean_return;
      }

      Serial.printf("[E] Frame (%d) send failed\n", Frame_Id);

    }

    return_val = true;
    goto capture_n_upload_clean_return;
  }

  // Coz consecutive, occasional frame send failures are expected
  Sender_FrameSendFailCount = 0;
  return_val = true;

capture_n_upload_clean_return:

  // Return the frame buffer to be reused
  esp_camera_fb_return(fb);
  return return_val;
}

void setup()
{
  init_board();
  init_serial();
  init_wifi();
  init_cam();
  config_cam();
}

void loop()
{
  static bool capture_or_dummy_mode = true;
  static uint64_t dummy_counter = 0;

  if (capture_or_dummy_mode) {
    esp_wifi_set_ps(WIFI_PS_NONE);
    capture_or_dummy_mode = capture_n_upload();
    delay(CAPTURE_N_UPLOAD_DELAY_MS);
  } else {
    Serial.printf("Seconds Elapsed Counter = %d\n", ++dummy_counter);
    delay(1000);
  }

}
