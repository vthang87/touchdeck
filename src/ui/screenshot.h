#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// Capture the active LVGL screen into a top-down RGB565 BMP in PSRAM.
// Caller must free(*out_bmp) with heap_caps_free / free via uiScreenshotFree().
bool uiScreenshotCaptureBmp(uint8_t** out_bmp, size_t* out_len);
void uiScreenshotFree(uint8_t* bmp);
