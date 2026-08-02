#include "screenshot.h"

#include <string.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "board_config.h"
#include "display/display_driver.h"
#include "system/idle_manager.h"
#include "ui/screens/clock_screen.h"
#include "ui/screens/home_grid_screen.h"
#include "ui/screens/workspace_pager.h"

namespace {

#pragma pack(push, 1)
struct BmpFileHeader {
  uint16_t bfType;
  uint32_t bfSize;
  uint16_t bfReserved1;
  uint16_t bfReserved2;
  uint32_t bfOffBits;
};

struct BmpInfoHeader {
  uint32_t biSize;
  int32_t biWidth;
  int32_t biHeight;
  uint16_t biPlanes;
  uint16_t biBitCount;
  uint32_t biCompression;
  uint32_t biSizeImage;
  int32_t biXPelsPerMeter;
  int32_t biYPelsPerMeter;
  uint32_t biClrUsed;
  uint32_t biClrImportant;
};
#pragma pack(pop)

static constexpr uint32_t kBiBitfields = 3;
static constexpr size_t kBmpHeaderSize =
    sizeof(BmpFileHeader) + sizeof(BmpInfoHeader) + (3 * sizeof(uint32_t));

}  // namespace

bool uiScreenshotCaptureBmp(uint8_t** out_bmp, size_t* out_len) {
  if (!out_bmp || !out_len) {
    return false;
  }
  *out_bmp = nullptr;
  *out_len = 0;

  // Wake / leave clock so the home grid is what we capture.
  idleManagerNoteActivity();
  if (clockScreenIsVisible()) {
    clockScreenHide();
  }
  idleManagerNoteRelease();
  displayDriverSetBacklight(100);

  if (!displayDriverLock(500)) {
    Serial.println("[SHOT] lock timeout");
    return false;
  }

  lv_obj_t* scr = workspacePagerRoot();
  if (!scr) {
    scr = homeGridScreenRoot();
  }
  if (!scr) {
    scr = lv_scr_act();
  } else if (lv_scr_act() != scr) {
    lv_scr_load(scr);
  }
  lv_obj_invalidate(scr);
  lv_refr_now(nullptr);

  const lv_coord_t w = lv_obj_get_width(scr);
  const lv_coord_t h = lv_obj_get_height(scr);
  if (w <= 0 || h <= 0) {
    displayDriverUnlock();
    return false;
  }

  const uint32_t px_bytes = lv_snapshot_buf_size_needed(scr, LV_IMG_CF_TRUE_COLOR);
  if (px_bytes == 0) {
    displayDriverUnlock();
    Serial.println("[SHOT] buf size 0");
    return false;
  }

  void* px = heap_caps_malloc(px_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!px) {
    displayDriverUnlock();
    Serial.printf("[SHOT] PSRAM alloc %u failed\n", px_bytes);
    return false;
  }

  lv_img_dsc_t dsc{};
  const lv_res_t res =
      lv_snapshot_take_to_buf(scr, LV_IMG_CF_TRUE_COLOR, &dsc, px, px_bytes);
  displayDriverUnlock();

  if (res != LV_RES_OK) {
    heap_caps_free(px);
    Serial.println("[SHOT] snapshot failed");
    return false;
  }

  // RGB565 rows are already 2-byte aligned; BMP stride = width * 2.
  const uint32_t row_bytes = static_cast<uint32_t>(w) * 2U;
  const uint32_t image_bytes = row_bytes * static_cast<uint32_t>(h);
  const size_t total = kBmpHeaderSize + image_bytes;

  uint8_t* bmp = static_cast<uint8_t*>(
      heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!bmp) {
    heap_caps_free(px);
    Serial.println("[SHOT] BMP alloc failed");
    return false;
  }

  auto* fh = reinterpret_cast<BmpFileHeader*>(bmp);
  auto* ih = reinterpret_cast<BmpInfoHeader*>(bmp + sizeof(BmpFileHeader));
  uint32_t* masks = reinterpret_cast<uint32_t*>(bmp + sizeof(BmpFileHeader) + sizeof(BmpInfoHeader));

  fh->bfType = 0x4D42;  // 'BM'
  fh->bfSize = static_cast<uint32_t>(total);
  fh->bfReserved1 = 0;
  fh->bfReserved2 = 0;
  fh->bfOffBits = kBmpHeaderSize;

  ih->biSize = sizeof(BmpInfoHeader);
  ih->biWidth = w;
  ih->biHeight = -h;  // top-down
  ih->biPlanes = 1;
  ih->biBitCount = 16;
  ih->biCompression = kBiBitfields;
  ih->biSizeImage = image_bytes;
  ih->biXPelsPerMeter = 2835;
  ih->biYPelsPerMeter = 2835;
  ih->biClrUsed = 0;
  ih->biClrImportant = 0;

  masks[0] = 0x0000F800;  // R
  masks[1] = 0x000007E0;  // G
  masks[2] = 0x0000001F;  // B

  memcpy(bmp + kBmpHeaderSize, px, image_bytes);
  heap_caps_free(px);

  *out_bmp = bmp;
  *out_len = total;
  Serial.printf("[SHOT] %dx%d BMP %u bytes\n", w, h, static_cast<unsigned>(total));
  return true;
}

void uiScreenshotFree(uint8_t* bmp) {
  if (bmp) {
    heap_caps_free(bmp);
  }
}
