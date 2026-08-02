#include "touch_gt911.h"

#include <Wire.h>

#include "board_config.h"
#include "system/idle_manager.h"

static uint8_t s_touch_addr = BOARD_TOUCH_I2C_ADDR_PRIMARY;
static lv_indev_drv_t s_indev_drv;
static bool s_ok = false;
static uint16_t s_last_x = 0;
static uint16_t s_last_y = 0;

static bool gt911ReadReg(uint16_t reg, uint8_t* data, size_t len) {
  Wire.beginTransmission(s_touch_addr);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  const size_t got = Wire.requestFrom(s_touch_addr, static_cast<size_t>(len));
  if (got != len) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    data[i] = Wire.read();
  }
  return true;
}

static bool gt911WriteReg(uint16_t reg, uint8_t value) {
  Wire.beginTransmission(s_touch_addr);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static bool probeAddress(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Vendor TAMC_GT911::reset() — RST on GPIO38 selects address and wakes controller
static void gt911HardwareReset(uint8_t prefer_addr) {
  if (BOARD_TOUCH_RST_PIN < 0) {
    return;
  }
  if (BOARD_TOUCH_INT_PIN >= 0) {
    pinMode(BOARD_TOUCH_INT_PIN, OUTPUT);
    digitalWrite(BOARD_TOUCH_INT_PIN, LOW);
  }
  pinMode(BOARD_TOUCH_RST_PIN, OUTPUT);
  digitalWrite(BOARD_TOUCH_RST_PIN, LOW);
  delay(10);
  if (BOARD_TOUCH_INT_PIN >= 0) {
    digitalWrite(BOARD_TOUCH_INT_PIN, prefer_addr == BOARD_TOUCH_I2C_ADDR_SECONDARY ? HIGH : LOW);
    delay(1);
  }
  digitalWrite(BOARD_TOUCH_RST_PIN, HIGH);
  delay(5);
  if (BOARD_TOUCH_INT_PIN >= 0) {
    digitalWrite(BOARD_TOUCH_INT_PIN, LOW);
    delay(50);
    pinMode(BOARD_TOUCH_INT_PIN, INPUT);
  }
  delay(50);
}

static void i2cScanLog() {
  uint8_t found = 0;
  Serial.print("[TOUCH] I2C scan:");
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", addr);
      ++found;
    }
  }
  if (!found) {
    Serial.print(" (none)");
  }
  Serial.println();
}

bool touchGt911Begin() {
  pinMode(BOARD_TOUCH_I2C_SDA, INPUT_PULLUP);
  pinMode(BOARD_TOUCH_I2C_SCL, INPUT_PULLUP);
  delay(10);

  gt911HardwareReset(BOARD_TOUCH_I2C_ADDR_PRIMARY);

  Wire.begin(BOARD_TOUCH_I2C_SDA, BOARD_TOUCH_I2C_SCL, BOARD_TOUCH_I2C_FREQ_HZ);
  Wire.setTimeOut(50);
  delay(50);

  i2cScanLog();

  if (probeAddress(BOARD_TOUCH_I2C_ADDR_PRIMARY)) {
    s_touch_addr = BOARD_TOUCH_I2C_ADDR_PRIMARY;
  } else if (probeAddress(BOARD_TOUCH_I2C_ADDR_SECONDARY)) {
    s_touch_addr = BOARD_TOUCH_I2C_ADDR_SECONDARY;
  } else {
    Serial.println("[TOUCH] GT911 not found on I2C");
    s_ok = false;
    return false;
  }

  uint8_t id[4] = {};
  if (gt911ReadReg(0x8140, id, 4)) {
    Serial.printf("[TOUCH] GT911 id=%c%c%c addr=0x%02X rst=%d\n", id[0], id[1], id[2], s_touch_addr,
                  BOARD_TOUCH_RST_PIN);
  }

  gt911WriteReg(0x814E, 0);
  s_ok = true;
  return true;
}

static bool s_was_pressed = false;

static void touchReadCb(lv_indev_drv_t* /*drv*/, lv_indev_data_t* data) {
  data->state = LV_INDEV_STATE_RELEASED;
  data->point.x = s_last_x;
  data->point.y = s_last_y;

  auto noteReleased = [&]() {
    if (s_was_pressed) {
      s_was_pressed = false;
      idleManagerNoteRelease();
    }
  };

  if (!s_ok) {
    noteReleased();
    return;
  }

  uint8_t buf[1 + 8] = {};
  if (!gt911ReadReg(0x814E, buf, 1)) {
    noteReleased();
    return;
  }

  if ((buf[0] & 0x80) == 0) {
    gt911WriteReg(0x814E, 0);
    noteReleased();
    return;
  }

  const uint8_t points = buf[0] & 0x0F;
  if (points == 0 || points > 5) {
    gt911WriteReg(0x814E, 0);
    noteReleased();
    return;
  }

  if (!gt911ReadReg(0x814F, &buf[1], 8)) {
    gt911WriteReg(0x814E, 0);
    noteReleased();
    return;
  }
  gt911WriteReg(0x814E, 0);

  uint16_t x = static_cast<uint16_t>(buf[2]) | (static_cast<uint16_t>(buf[3]) << 8);
  uint16_t y = static_cast<uint16_t>(buf[4]) | (static_cast<uint16_t>(buf[5]) << 8);

  // Vendor Touch_GT911 ROTATION_NORMAL: x = width - x; y = height - y
#if BOARD_TOUCH_INVERT_X
  if (x < BOARD_LCD_H_RES) {
    x = BOARD_LCD_H_RES - 1 - x;
  }
#endif
#if BOARD_TOUCH_INVERT_Y
  if (y < BOARD_LCD_V_RES) {
    y = BOARD_LCD_V_RES - 1 - y;
  }
#endif

  if (x >= BOARD_LCD_H_RES) {
    x = BOARD_LCD_H_RES - 1;
  }
  if (y >= BOARD_LCD_V_RES) {
    y = BOARD_LCD_V_RES - 1;
  }

  s_last_x = x;
  s_last_y = y;
  data->point.x = x;
  data->point.y = y;
  data->state = LV_INDEV_STATE_PRESSED;
  s_was_pressed = true;
  idleManagerNoteActivity();
}

void touchGt911RegisterLvgl() {
  lv_indev_drv_init(&s_indev_drv);
  s_indev_drv.type = LV_INDEV_TYPE_POINTER;
  s_indev_drv.read_cb = touchReadCb;
  lv_indev_drv_register(&s_indev_drv);
  Serial.println("[TOUCH] LVGL indev registered (vendor map)");
}
