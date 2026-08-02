#pragma once

// JC8048W550C — pins/timings from vendor demo:
// /Volumes/1TB_SSD/R&D/JC8048W550/1-Demo/Demo_Arduino/7_1_lvgl_music_gt911_5.0

#define BOARD_LCD_H_RES           800
#define BOARD_LCD_V_RES           480

#define BOARD_LCD_HSYNC           39
#define BOARD_LCD_VSYNC           41
#define BOARD_LCD_DE              40
#define BOARD_LCD_PCLK            42

// RGB565 via Arduino_GFX order: R0-R4, G0-G5, B0-B4
#define BOARD_LCD_R0              45
#define BOARD_LCD_R1              48
#define BOARD_LCD_R2              47
#define BOARD_LCD_R3              21
#define BOARD_LCD_R4              14
#define BOARD_LCD_G0              5
#define BOARD_LCD_G1              6
#define BOARD_LCD_G2              7
#define BOARD_LCD_G3              15
#define BOARD_LCD_G4              16
#define BOARD_LCD_G5              4
#define BOARD_LCD_B0              8
#define BOARD_LCD_B1              3
#define BOARD_LCD_B2              46
#define BOARD_LCD_B3              9
#define BOARD_LCD_B4              1

// Vendor music demo: 12 MHz + porch 8/4/8
#define BOARD_LCD_PCLK_HZ         (12 * 1000 * 1000)
#define BOARD_LCD_HSYNC_PULSE_WIDTH  4
#define BOARD_LCD_HSYNC_BACK_PORCH   8
#define BOARD_LCD_HSYNC_FRONT_PORCH  8
#define BOARD_LCD_VSYNC_PULSE_WIDTH  4
#define BOARD_LCD_VSYNC_BACK_PORCH   8
#define BOARD_LCD_VSYNC_FRONT_PORCH  8
#define BOARD_LCD_PCLK_ACTIVE_NEG    1

#define BOARD_BACKLIGHT_PIN       2
#define BOARD_BACKLIGHT_ON_LEVEL  1
// The LED boost driver on this panel collapses at high PWM rates — keep it low.
#define BOARD_BACKLIGHT_PWM_HZ    1000

// GT911 — vendor touch.h
#define BOARD_TOUCH_I2C_SDA       19
#define BOARD_TOUCH_I2C_SCL       20
#define BOARD_TOUCH_I2C_FREQ_HZ   400000
#define BOARD_TOUCH_I2C_ADDR_PRIMARY   0x5D
#define BOARD_TOUCH_I2C_ADDR_SECONDARY 0x14
#define BOARD_TOUCH_RST_PIN       38
#define BOARD_TOUCH_INT_PIN       (-1)
// GT911 on this panel already reports screen-oriented coordinates
#define BOARD_TOUCH_INVERT_X      0
#define BOARD_TOUCH_INVERT_Y      0

// ESP32-S3 USB D-/D+ = GPIO19/20 = touch I2C → keep USB HID off
#define BOARD_USB_HID_ENABLED     0

#define BOARD_MODEL_NAME          "JC8048W550C"
