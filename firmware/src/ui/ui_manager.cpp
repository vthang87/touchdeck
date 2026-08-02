#include "ui_manager.h"

#include "display/display_driver.h"
#include "display/touch_gt911.h"
#include "screens/workspace_pager.h"
#include "screens/clock_screen.h"
#include "screens/media_screen.h"
#include "screens/notification_overlay.h"
#include "storage/profile_store.h"
#include "storage/icon_store.h"
#include "storage/settings_store.h"
#include "system/idle_manager.h"
#include "ble/ble_manager.h"

bool uiManagerBegin() {
  if (!profileStore.begin()) {
    Serial.println("[UI] Filesystem unavailable — using in-memory defaults");
  }
  iconStoreBegin();

  if (!displayDriverBegin()) {
    return false;
  }

  const bool touch_ok = touchGt911Begin();
  if (touch_ok) {
    touchGt911RegisterLvgl();
  } else {
    Serial.println("[UI] Continuing without touch");
  }

  if (!workspacePagerCreate()) {
    Serial.println("[UI] Workspace pager create failed");
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
    workspacePagerTick();
    mediaScreenSetLinked(bleManagerIsConnected());
  }
}

bool uiManagerReloadGrid() {
  workspacePagerSyncFromStore();
  return workspacePagerReloadShortcuts();
}

bool uiManagerSetPageCount(uint8_t count) {
  return workspacePagerSetPageCount(count);
}

void uiManagerSetPage(uint8_t index) {
  workspacePagerSetPage(index);
}
