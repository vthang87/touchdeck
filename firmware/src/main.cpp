#include <Arduino.h>

#include "app/app_manager.h"

// DeckProfile + LVGL tileview/media/grid init need more than the SDK default 8KB.
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

void setup() {
  appManager.begin();
}

void loop() {
  appManager.loop();
}
