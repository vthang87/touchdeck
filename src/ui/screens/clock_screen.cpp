#include "clock_screen.h"

#include <lvgl.h>
#include <time.h>

#include "app_config.h"
#include "board_config.h"
#include "display/display_driver.h"
#include "ui/fonts/clock_fonts.h"
#include "ui/screens/home_grid_screen.h"

namespace {

lv_obj_t* s_scr = nullptr;
lv_obj_t* s_time = nullptr;
lv_obj_t* s_date = nullptr;
bool s_visible = false;
uint32_t s_last_paint_ms = 0;

const lv_font_t* fontForSize(uint8_t px) {
  switch (px) {
    case 48:
      return &lv_font_montserrat_48;
    case 72:
      return &clock_font_72;
    case 128:
      return &clock_font_128;
    case 160:
      return &clock_font_160;
    case 96:
    default:
      return &clock_font_96;
  }
}

void applyFontLocked(uint8_t px) {
  if (!s_time || !s_date) {
    return;
  }
  const lv_font_t* face = fontForSize(px);
  lv_obj_set_style_text_font(s_time, face, 0);
  // Date sits under the digits; keep it readable but clearly secondary.
  lv_obj_set_style_text_font(s_date, px >= 128 ? &lv_font_montserrat_28 : &lv_font_montserrat_20, 0);

  const lv_coord_t half = static_cast<lv_coord_t>(lv_font_get_line_height(face) / 2);
  lv_obj_align(s_time, LV_ALIGN_CENTER, 0, static_cast<lv_coord_t>(-half / 2));
  lv_obj_align(s_date, LV_ALIGN_CENTER, 0, static_cast<lv_coord_t>(half / 2 + 24));
}

void paintLocked() {
  if (!s_time || !s_date) {
    return;
  }
  time_t now = time(nullptr);
  struct tm local {};
  if (now < 1700000000 || !localtime_r(&now, &local)) {
    lv_label_set_text(s_time, "--:--");
    lv_label_set_text(s_date, "Waiting for time...");
    return;
  }
  char time_buf[16];
  char date_buf[48];
  strftime(time_buf, sizeof(time_buf), "%H:%M", &local);
  strftime(date_buf, sizeof(date_buf), "%A, %d %b %Y", &local);
  lv_label_set_text(s_time, time_buf);
  lv_label_set_text(s_date, date_buf);
}

}  // namespace

bool clockScreenCreate() {
  s_scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x05080F), 0);
  lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

  s_time = lv_label_create(s_scr);
  lv_obj_set_style_text_color(s_time, lv_color_hex(0xF8FAFC), 0);
  lv_label_set_text(s_time, "--:--");

  s_date = lv_label_create(s_scr);
  lv_obj_set_style_text_color(s_date, lv_color_hex(0x94A3B8), 0);
  lv_label_set_text(s_date, "");

  applyFontLocked(APP_CLOCK_FONT_PX_DEFAULT);

  lv_obj_t* hint = lv_label_create(s_scr);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(hint, lv_color_hex(0x475569), 0);
  lv_label_set_text(hint, "Touch to wake");
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -24);

  s_visible = false;
  return true;
}

void clockScreenShow() {
  if (!s_scr || s_visible) {
    return;
  }
  if (!displayDriverLock(200)) {
    return;
  }
  paintLocked();
  lv_scr_load(s_scr);
  s_visible = true;
  s_last_paint_ms = millis();
  displayDriverUnlock();
}

void clockScreenHide() {
  if (!s_visible) {
    return;
  }
  if (!displayDriverLock(200)) {
    return;
  }
  lv_obj_t* root = homeGridScreenRoot();
  if (root) {
    lv_scr_load(root);
  }
  s_visible = false;
  displayDriverUnlock();
}

void clockScreenTick() {
  if (!s_visible) {
    return;
  }
  const uint32_t now = millis();
  if (now - s_last_paint_ms < 500) {
    return;
  }
  s_last_paint_ms = now;
  if (displayDriverLock(30)) {
    paintLocked();
    displayDriverUnlock();
  }
}

bool clockScreenIsVisible() { return s_visible; }

void clockScreenSetFontSize(uint8_t px) {
  if (!s_scr) {
    return;
  }
  if (!displayDriverLock(200)) {
    return;
  }
  applyFontLocked(px);
  displayDriverUnlock();
}
