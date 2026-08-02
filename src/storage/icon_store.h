#pragma once

#include <Arduino.h>
#include <lvgl.h>

// SD-backed RGB565 icon storage.
//
// File format (/icons/<id>.bin on a FAT32 microSD):
//   offset 0: magic 'T','D','I','1'
//   offset 4: uint16 LE width
//   offset 6: uint16 LE height
//   offset 8: width*height * uint16 LE  (RGB565, native lv_color_t byte order)
//
// Icons are square-ish and small (<= ICON_MAX_DIM). Pixel data is cached in
// PSRAM as an lv_img_dsc_t so the home grid can point tiles at it directly.

#define ICON_DIR "/icons"
#define ICON_MAGIC "TDI1"
#define ICON_MAX_DIM 128
#define ICON_MAX_BYTES (8 + ICON_MAX_DIM * ICON_MAX_DIM * 2)

bool iconStoreBegin();
bool iconStoreReady();

// True if /icons/<id>.bin exists on the card.
bool iconStoreHas(const char* id);

// Returns a cached descriptor for <id>, loading it from SD on first use.
// Returns nullptr if the card is absent, the file is missing, or it is invalid.
const lv_img_dsc_t* iconStoreGet(const char* id);

// Streaming write used by the multipart upload handler.
bool iconStoreWriteBegin(const char* id);
bool iconStoreWriteChunk(const uint8_t* data, size_t len);
bool iconStoreWriteEnd();
void iconStoreWriteAbort();

bool iconStoreDelete(const char* id);

// Drops the in-RAM cache so the next iconStoreGet reloads from SD.
void iconStoreInvalidate();

// JSON array of available icon ids, e.g. ["cursor","slack"].
String iconStoreListJson();
