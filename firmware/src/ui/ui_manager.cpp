#include "ui_manager.h"

#include "display/display_driver.h"
#include "display/touch_gt911.h"
#include "screens/home_grid_screen.h"
#include "screens/clock_screen.h"
#include "screens/notification_overlay.h"
#include "storage/profile_store.h"
#include "storage/icon_store.h"
#include "storage/settings_store.h"
#include "system/idle_manager.h"

bool uiManagerBegin() {
  if (!profileStore.begin()) {
    Serial.println("[UI] Filesystem unavailable — using in-memory defaults");
  }
  iconStoreBegin();  // Optional microSD icons; grid falls back to built-in glyphs.

  if (!displayDriverBegin()) {
    return false;
  }

  const bool touch_ok = touchGt911Begin();
  if (touch_ok) {
    touchGt911RegisterLvgl();
  } else {
    Serial.println("[UI] Continuing without touch");
  }

  // Build UI before LVGL task starts to avoid cross-thread widget creation.
  if (!homeGridScreenCreate()) {
    Serial.println("[UI] Home grid create failed");
    return false;
  }
  if (!clockScreenCreate()) {
    Serial.println("[UI] Clock screen create failed");
    return false;
  }
  notificationOverlayBegin();

  const DeviceSettings settings = settingsStore.load();
  clockScreenSetFontSize(settings.clock_font_px);
  idleManagerBegin(settings);
  displayDriverStartTask();
  return true;
}

void uiManagerTick() {
  displayDriverTick();
  idleManagerTick();
  notificationOverlayTick();
  if (!clockScreenIsVisible()) {
    homeGridScreenTick();
  }
}

bool uiManagerReloadGrid() {
  return homeGridScreenReload(profileStore.current());
}
