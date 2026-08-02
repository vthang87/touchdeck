#include "volume_screen.h"

#include "app_config.h"
#include "app/input_queue.h"

static lv_obj_t* s_bar = nullptr;
static lv_obj_t* s_label = nullptr;
static int s_volume = APP_VOLUME_DEFAULT;
static bool s_muted = false;

static void updateChrome() {
  if (s_bar) {
    lv_bar_set_value(s_bar, s_muted ? 0 : s_volume, LV_ANIM_OFF);
  }
  if (s_label) {
    if (s_muted) {
      lv_label_set_text(s_label, "MUTED");
    } else {
      lv_label_set_text_fmt(s_label, "%d%%", s_volume);
    }
  }
}

static void onDown(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
    return;
  }
  Serial.println("[UI] vol_down");
  inputQueue.push(InputAction::VolumeDown);
  s_muted = false;
  s_volume = max(APP_VOLUME_MIN, s_volume - APP_VOLUME_STEP);
  updateChrome();
}

static void onUp(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
    return;
  }
  Serial.println("[UI] vol_up");
  inputQueue.push(InputAction::VolumeUp);
  s_muted = false;
  s_volume = min(APP_VOLUME_MAX, s_volume + APP_VOLUME_STEP);
  updateChrome();
}

static void onMute(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
    return;
  }
  Serial.println("[UI] mute");
  inputQueue.push(InputAction::MuteToggle);
  s_muted = !s_muted;
  updateChrome();
}

static lv_obj_t* makeButton(lv_obj_t* parent, const char* text, lv_align_t align, lv_coord_t x_ofs,
                            lv_event_cb_t cb, const lv_font_t* font = &lv_font_montserrat_48) {
  lv_obj_t* btn = lv_btn_create(parent);
  lv_obj_set_size(btn, 220, 220);
  lv_obj_align(btn, align, x_ofs, -20);
  lv_obj_set_style_radius(btn, 16, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x334155), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_PRESSED, nullptr);

  lv_obj_t* label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0xF8FAFC), 0);
  lv_obj_center(label);
  return btn;
}

void volumeScreenCreate() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1220), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  lv_obj_t* title = lv_label_create(scr);
  lv_label_set_text(title, "TouchDeck");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xE2E8F0), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

  lv_obj_t* subtitle = lv_label_create(scr);
  lv_label_set_text(subtitle, "Volume");
  lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(subtitle, lv_color_hex(0x94A3B8), 0);
  lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 64);

  makeButton(scr, "-", LV_ALIGN_CENTER, -260, onDown);
  makeButton(scr, "MUTE", LV_ALIGN_CENTER, 0, onMute, &lv_font_montserrat_28);
  makeButton(scr, "+", LV_ALIGN_CENTER, 260, onUp);

  s_bar = lv_bar_create(scr);
  lv_obj_set_size(s_bar, 640, 28);
  lv_obj_align(s_bar, LV_ALIGN_BOTTOM_MID, 0, -72);
  lv_bar_set_range(s_bar, 0, 100);
  lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x1E293B), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x38BDF8), LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_bar, 8, LV_PART_MAIN);
  lv_obj_set_style_radius(s_bar, 8, LV_PART_INDICATOR);

  s_label = lv_label_create(scr);
  lv_obj_set_style_text_font(s_label, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(s_label, lv_color_hex(0xF8FAFC), 0);
  lv_obj_align(s_label, LV_ALIGN_BOTTOM_MID, 0, -28);

  updateChrome();
  Serial.println("[UI] Volume screen ready");
}

void volumeScreenSetLevel(int volume, bool muted) {
  s_volume = constrain(volume, APP_VOLUME_MIN, APP_VOLUME_MAX);
  s_muted = muted;
  updateChrome();
}

int volumeScreenGetLevel() { return s_volume; }

bool volumeScreenIsMuted() { return s_muted; }
