#include "display_driver.h"

#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "board_config.h"

static Arduino_ESP32RGBPanel* s_bus = nullptr;
static Arduino_RGB_Display* s_gfx = nullptr;
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static lv_color_t* s_buf1 = nullptr;
static lv_color_t* s_buf2 = nullptr;
static SemaphoreHandle_t s_lvgl_mutex = nullptr;
static TaskHandle_t s_lvgl_task = nullptr;
static uint8_t s_brightness = 100;

static constexpr int kBlLedcChannel = 0;
static constexpr int kBlLedcResolution = 8;  // 0..255
static uint32_t s_bl_freq_hz = BOARD_BACKLIGHT_PWM_HZ;

static void lvglFlushCb(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_map) {
  const int16_t w = static_cast<int16_t>(area->x2 - area->x1 + 1);
  const int16_t h = static_cast<int16_t>(area->y2 - area->y1 + 1);
  s_gfx->draw16bitRGBBitmap(static_cast<int16_t>(area->x1), static_cast<int16_t>(area->y1),
                            reinterpret_cast<uint16_t*>(&color_map->full), w, h);
  lv_disp_flush_ready(disp);
}

static void initBacklight() {
  ledcSetup(kBlLedcChannel, s_bl_freq_hz, kBlLedcResolution);
  ledcAttachPin(BOARD_BACKLIGHT_PIN, kBlLedcChannel);
}

void displayDriverSetBacklightFreq(uint32_t hz) {
  if (hz < 100) {
    hz = 100;
  }
  s_bl_freq_hz = hz;
  ledcSetup(kBlLedcChannel, s_bl_freq_hz, kBlLedcResolution);
  displayDriverSetBacklight(displayDriverGetBacklight());
  Serial.printf("[DISP] backlight PWM %luHz\n", static_cast<unsigned long>(s_bl_freq_hz));
}

uint32_t displayDriverGetBacklightFreq() { return s_bl_freq_hz; }

void displayDriverSetBacklight(uint8_t percent) {
  if (percent > 100) {
    percent = 100;
  }
  s_brightness = percent;
  uint32_t duty = (static_cast<uint32_t>(percent) * 255U) / 100U;
  if (BOARD_BACKLIGHT_ON_LEVEL == 0) {
    duty = 255U - duty;
  }
  ledcWrite(kBlLedcChannel, duty);
}

uint8_t displayDriverGetBacklight() { return s_brightness; }

static void lvglTask(void* /*arg*/) {
  for (;;) {
    if (xSemaphoreTakeRecursive(s_lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      lv_timer_handler();
      xSemaphoreGiveRecursive(s_lvgl_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

bool displayDriverLock(uint32_t timeout_ms) {
  if (!s_lvgl_mutex) {
    return true;
  }
  return xSemaphoreTakeRecursive(s_lvgl_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void displayDriverUnlock() {
  if (s_lvgl_mutex) {
    xSemaphoreGiveRecursive(s_lvgl_mutex);
  }
}

bool displayDriverBegin() {
  initBacklight();
  s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();

  // Vendor JC8048W550 pins + music-demo timings (12 MHz, porch 8/4/8), GFX 1.4.x API
  s_bus = new Arduino_ESP32RGBPanel(
      BOARD_LCD_DE, BOARD_LCD_VSYNC, BOARD_LCD_HSYNC, BOARD_LCD_PCLK,
      BOARD_LCD_R0, BOARD_LCD_R1, BOARD_LCD_R2, BOARD_LCD_R3, BOARD_LCD_R4,
      BOARD_LCD_G0, BOARD_LCD_G1, BOARD_LCD_G2, BOARD_LCD_G3, BOARD_LCD_G4, BOARD_LCD_G5,
      BOARD_LCD_B0, BOARD_LCD_B1, BOARD_LCD_B2, BOARD_LCD_B3, BOARD_LCD_B4,
      0 /* hsync_polarity */, BOARD_LCD_HSYNC_FRONT_PORCH, BOARD_LCD_HSYNC_PULSE_WIDTH,
      BOARD_LCD_HSYNC_BACK_PORCH, 0 /* vsync_polarity */, BOARD_LCD_VSYNC_FRONT_PORCH,
      BOARD_LCD_VSYNC_PULSE_WIDTH, BOARD_LCD_VSYNC_BACK_PORCH, BOARD_LCD_PCLK_ACTIVE_NEG,
      BOARD_LCD_PCLK_HZ);

  s_gfx = new Arduino_RGB_Display(BOARD_LCD_H_RES, BOARD_LCD_V_RES, s_bus, 0 /* rotation */,
                                  true /* auto_flush */);

  if (!s_gfx->begin()) {
    Serial.println("[DISP] Arduino_GFX begin failed");
    return false;
  }

  s_gfx->fillScreen(BLACK);
  displayDriverSetBacklight(100);
  lv_init();

  // 15 lines/buffer (~24 KB each): smoother swipes than 10; leave headroom for Wi-Fi.
  const size_t buf_pixels = BOARD_LCD_H_RES * 15;
  s_buf1 = static_cast<lv_color_t*>(
      heap_caps_malloc(sizeof(lv_color_t) * buf_pixels, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  s_buf2 = static_cast<lv_color_t*>(
      heap_caps_malloc(sizeof(lv_color_t) * buf_pixels, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!s_buf1 || !s_buf2) {
    if (s_buf1) {
      heap_caps_free(s_buf1);
      s_buf1 = nullptr;
    }
    if (s_buf2) {
      heap_caps_free(s_buf2);
      s_buf2 = nullptr;
    }
    s_buf1 = static_cast<lv_color_t*>(
        heap_caps_malloc(sizeof(lv_color_t) * buf_pixels, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    s_buf2 = static_cast<lv_color_t*>(
        heap_caps_malloc(sizeof(lv_color_t) * buf_pixels, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    Serial.println("[DISP] LVGL buffers in PSRAM (fallback) — expect flicker");
  } else {
    Serial.printf("[DISP] LVGL buffers in internal SRAM (%u px each)\n",
                  static_cast<unsigned>(buf_pixels));
  }
  if (!s_buf1 || !s_buf2) {
    Serial.println("[DISP] Failed to allocate LVGL draw buffers");
    return false;
  }

  lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, buf_pixels);
  lv_disp_drv_init(&s_disp_drv);
  s_disp_drv.hor_res = BOARD_LCD_H_RES;
  s_disp_drv.ver_res = BOARD_LCD_V_RES;
  s_disp_drv.flush_cb = lvglFlushCb;
  s_disp_drv.draw_buf = &s_draw_buf;
  lv_disp_drv_register(&s_disp_drv);

  Serial.println("[DISP] Arduino_GFX RGB + LVGL ready (vendor JC8048W550 config)");
  return true;
}

void displayDriverStartTask() {
  if (s_lvgl_task) {
    return;
  }
  xTaskCreatePinnedToCore(lvglTask, "lvgl", 8192, nullptr, 5, &s_lvgl_task, 1);
  Serial.println("[DISP] LVGL task on core 1");
}

void displayDriverTick() {}
