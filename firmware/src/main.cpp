#include <Arduino.h>

#include "app/app_manager.h"

void setup() {
  appManager.begin();
}

void loop() {
  appManager.loop();
}
