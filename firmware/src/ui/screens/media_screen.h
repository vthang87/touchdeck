#pragma once

#include <lvgl.h>
#include <stdint.h>

bool mediaScreenCreate(lv_obj_t* parent);
void mediaScreenSetNowPlaying(const char* title, const char* artist, bool playing, uint32_t pos_ms,
                              uint32_t dur_ms, const char* app, uint16_t rate_x100);
void mediaScreenSetVolume(int volume, bool muted);
void mediaScreenSetLinked(bool linked);
void mediaScreenTick();
bool mediaScreenIsPlaying();
const char* mediaScreenTitle();
const char* mediaScreenArtist();
lv_obj_t* mediaScreenRoot();
