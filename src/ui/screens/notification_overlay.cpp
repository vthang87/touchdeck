#include "notification_overlay.h"

#include <lvgl.h>
#include <string.h>

#include "display/display_driver.h"
#include "system/idle_manager.h"

namespace {

constexpr uint8_t kMaxItems = 2;

struct NotificationItem {
  char id[16];
  char source[12];
  char title[24];
  char body[48];
  bool active;
};

NotificationItem s_items[kMaxItems];
lv_obj_t* s_banner = nullptr;
lv_obj_t* s_icon = nullptr;
lv_obj_t* s_title = nullptr;
lv_obj_t* s_body = nullptr;
uint32_t s_last_pulse_ms = 0;
bool s_pulse_on = false;

uint8_t activeCount() {
  uint8_t n = 0;
  for (const NotificationItem& item : s_items) {
    if (item.active) {
      ++n;
    }
  }
  return n;
}

NotificationItem* findById(const char* id) {
  if (!id || !id[0]) {
    return nullptr;
  }
  for (NotificationItem& item : s_items) {
    if (item.active && strcmp(item.id, id) == 0) {
      return &item;
    }
  }
  return nullptr;
}

NotificationItem* findBySource(const char* source) {
  if (!source || !source[0]) {
    return nullptr;
  }
  for (NotificationItem& item : s_items) {
    if (item.active && strcmp(item.source, source) == 0) {
      return &item;
    }
  }
  return nullptr;
}

NotificationItem* allocSlot() {
  for (NotificationItem& item : s_items) {
    if (!item.active) {
      return &item;
    }
  }
  return &s_items[0];
}

const NotificationItem* newestActive() {
  for (const NotificationItem& item : s_items) {
    if (item.active) {
      return &item;
    }
  }
  return nullptr;
}

lv_color_t colorForSource(const char* source) {
  if (source && strcmp(source, "codex") == 0) {
    return lv_color_hex(0x0D8A6A);
  }
  if (source && strcmp(source, "cursor") == 0) {
    return lv_color_hex(0x6366F1);
  }
  return lv_color_hex(0xF59E0B);
}

void copyField(char* dst, size_t dst_len, const char* src) {
  if (!dst || dst_len == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dst_len - 1);
  dst[dst_len - 1] = '\0';
}

void refreshBannerLocked() {
  const NotificationItem* item = newestActive();
  if (!s_banner) {
    return;
  }
  if (!item) {
    lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_obj_clear_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_border_color(s_banner, colorForSource(item->source), 0);
  if (s_icon) {
    lv_label_set_text(s_icon, LV_SYMBOL_BELL);
    lv_obj_set_style_text_color(s_icon, colorForSource(item->source), 0);
  }
  if (s_title) {
    lv_label_set_text(s_title, item->title[0] ? item->title : "Approval");
  }
  if (s_body) {
    lv_label_set_text(s_body, item->body[0] ? item->body : "Waiting for approval");
  }
}

}  // namespace

void notificationOverlayBegin() {
  s_banner = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_banner, 760, 72);
  lv_obj_align(s_banner, LV_ALIGN_TOP_MID, 0, 8);
  lv_obj_set_style_radius(s_banner, 14, 0);
  lv_obj_set_style_bg_color(s_banner, lv_color_hex(0x111827), 0);
  lv_obj_set_style_bg_opa(s_banner, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_banner, 2, 0);
  lv_obj_set_style_border_color(s_banner, lv_color_hex(0xF59E0B), 0);
  lv_obj_set_style_pad_hor(s_banner, 14, 0);
  lv_obj_set_style_pad_ver(s_banner, 10, 0);
  lv_obj_clear_flag(s_banner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);

  s_icon = lv_label_create(s_banner);
  lv_label_set_text(s_icon, LV_SYMBOL_BELL);
  lv_obj_set_style_text_font(s_icon, &lv_font_montserrat_28, 0);
  lv_obj_align(s_icon, LV_ALIGN_LEFT_MID, 0, 0);

  s_title = lv_label_create(s_banner);
  lv_obj_set_style_text_font(s_title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(s_title, lv_color_hex(0xF8FAFC), 0);
  lv_obj_align(s_title, LV_ALIGN_TOP_LEFT, 44, 2);

  s_body = lv_label_create(s_banner);
  lv_obj_set_style_text_font(s_body, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_body, lv_color_hex(0xCBD5E1), 0);
  lv_obj_align(s_body, LV_ALIGN_BOTTOM_LEFT, 44, -2);
}

void notificationOverlaySet(const char* id, const char* source, const char* title, const char* body) {
  if (!id || !id[0]) {
    return;
  }
  idleManagerWakeForAlert();

  NotificationItem* item = findById(id);
  if (!item && source && source[0]) {
    item = findBySource(source);
  }
  if (!item) {
    item = allocSlot();
  }

  item->active = true;
  copyField(item->id, sizeof(item->id), id);
  copyField(item->source, sizeof(item->source), source ? source : id);
  copyField(item->title, sizeof(item->title), title);
  copyField(item->body, sizeof(item->body), body);

  if (displayDriverLock(50)) {
    refreshBannerLocked();
    displayDriverUnlock();
  }
  Serial.printf("[NOTIFY] set id=%s source=%s title=%s\n", item->id, item->source, item->title);
}

void notificationOverlayClear(const char* id) {
  if (!id || !id[0]) {
    return;
  }
  NotificationItem* item = findById(id);
  if (!item && id[0]) {
    item = findBySource(id);
  }
  if (!item) {
    return;
  }
  item->active = false;
  item->id[0] = '\0';

  if (displayDriverLock(50)) {
    refreshBannerLocked();
    displayDriverUnlock();
  }
  Serial.printf("[NOTIFY] clear id=%s\n", id);
}

void notificationOverlayClearAll() {
  for (NotificationItem& item : s_items) {
    item.active = false;
    item.id[0] = '\0';
  }
  if (displayDriverLock(50)) {
    refreshBannerLocked();
    displayDriverUnlock();
  }
  Serial.println("[NOTIFY] clear all");
}

void notificationOverlayTick() {
  if (!s_banner || activeCount() == 0) {
    return;
  }
  const uint32_t now = millis();
  if (now - s_last_pulse_ms < 600) {
    return;
  }
  s_last_pulse_ms = now;
  s_pulse_on = !s_pulse_on;
  if (displayDriverLock(20)) {
    lv_obj_set_style_border_opa(s_banner, s_pulse_on ? LV_OPA_COVER : LV_OPA_50, 0);
    displayDriverUnlock();
  }
}

uint8_t notificationOverlayCount() { return activeCount(); }

bool notificationOverlayIsPending(const char* source) {
  return findBySource(source) != nullptr;
}
