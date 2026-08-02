#include "icon_store.h"

#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "grid_config.h"

// Vendor JC8048W550 SD wiring (shared SPI, panel is RGB parallel so the bus is free).
#define SD_PIN_CS 10
#define SD_PIN_MOSI 11
#define SD_PIN_SCK 12
#define SD_PIN_MISO 13
#define SD_SPI_HZ 20000000

namespace {

bool s_ready = false;
SPIClass s_spi(HSPI);

struct IconCacheEntry {
  char id[GRID_ICON_MAX + 1];
  lv_img_dsc_t dsc;
  uint8_t* pixels;  // PSRAM, owned
};

IconCacheEntry s_cache[GRID_MAX_TILES];
uint8_t s_cache_count = 0;

File s_write_file;
bool s_write_open = false;
String s_write_final;
String s_write_tmp;

bool validId(const char* id) {
  if (!id || !id[0]) {
    return false;
  }
  const size_t len = strlen(id);
  if (len > GRID_ICON_MAX) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    const char c = id[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '_' || c == '-';
    if (!ok) {
      return false;
    }
  }
  return true;
}

String pathFor(const char* id) {
  return String(ICON_DIR) + "/" + id + ".bin";
}

void freeCache() {
  for (uint8_t i = 0; i < s_cache_count; ++i) {
    if (s_cache[i].pixels) {
      heap_caps_free(s_cache[i].pixels);
      s_cache[i].pixels = nullptr;
    }
  }
  s_cache_count = 0;
}

IconCacheEntry* findCache(const char* id) {
  for (uint8_t i = 0; i < s_cache_count; ++i) {
    if (strcmp(s_cache[i].id, id) == 0) {
      return &s_cache[i];
    }
  }
  return nullptr;
}

const lv_img_dsc_t* loadFromSd(const char* id) {
  const String path = pathFor(id);
  if (!SD.exists(path)) {
    return nullptr;  // Avoid vfs open() error spam for built-in glyph tiles.
  }
  File f = SD.open(path, FILE_READ);
  if (!f) {
    return nullptr;
  }
  const size_t size = f.size();
  if (size < 8 || size > ICON_MAX_BYTES) {
    f.close();
    return nullptr;
  }

  uint8_t header[8];
  if (f.read(header, 8) != 8 || memcmp(header, ICON_MAGIC, 4) != 0) {
    f.close();
    return nullptr;
  }
  const uint16_t w = header[4] | (header[5] << 8);
  const uint16_t h = header[6] | (header[7] << 8);
  if (w == 0 || h == 0 || w > ICON_MAX_DIM || h > ICON_MAX_DIM) {
    f.close();
    return nullptr;
  }
  const size_t px_bytes = static_cast<size_t>(w) * h * 2;
  if (size - 8 < px_bytes) {
    f.close();
    return nullptr;
  }

  uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(px_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    buf = static_cast<uint8_t*>(malloc(px_bytes));
  }
  if (!buf) {
    f.close();
    return nullptr;
  }
  if (f.read(buf, px_bytes) != static_cast<int>(px_bytes)) {
    free(buf);
    f.close();
    return nullptr;
  }
  f.close();

  if (s_cache_count >= GRID_MAX_TILES) {
    // Simple policy: clear the whole cache when full, then reload lazily.
    freeCache();
  }
  IconCacheEntry& e = s_cache[s_cache_count++];
  memset(&e, 0, sizeof(e));
  strncpy(e.id, id, GRID_ICON_MAX);
  e.pixels = buf;
  e.dsc.header.always_zero = 0;
  e.dsc.header.w = w;
  e.dsc.header.h = h;
  e.dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  e.dsc.data_size = px_bytes;
  e.dsc.data = buf;
  return &e.dsc;
}

}  // namespace

bool iconStoreBegin() {
  if (s_ready) {
    return true;
  }
  s_spi.begin(SD_PIN_SCK, SD_PIN_MISO, SD_PIN_MOSI, SD_PIN_CS);
  if (!SD.begin(SD_PIN_CS, s_spi, SD_SPI_HZ)) {
    Serial.println("[SD] mount failed (no card?) — custom icons disabled");
    return false;
  }
  const uint8_t type = SD.cardType();
  if (type == CARD_NONE) {
    Serial.println("[SD] no card attached");
    return false;
  }
  if (!SD.exists(ICON_DIR)) {
    SD.mkdir(ICON_DIR);
  }
  s_ready = true;
  Serial.printf("[SD] mounted (%lluMB) — icons at %s\n",
                static_cast<unsigned long long>(SD.cardSize() / (1024ULL * 1024ULL)), ICON_DIR);
  return true;
}

bool iconStoreReady() { return s_ready; }

bool iconStoreHas(const char* id) {
  if (!s_ready || !validId(id)) {
    return false;
  }
  return SD.exists(pathFor(id));
}

const lv_img_dsc_t* iconStoreGet(const char* id) {
  if (!s_ready || !validId(id)) {
    return nullptr;
  }
  IconCacheEntry* cached = findCache(id);
  if (cached) {
    return &cached->dsc;
  }
  return loadFromSd(id);
}

bool iconStoreWriteBegin(const char* id) {
  if (!s_ready || !validId(id)) {
    return false;
  }
  if (s_write_open) {
    iconStoreWriteAbort();
  }
  s_write_final = pathFor(id);
  s_write_tmp = s_write_final + ".tmp";
  SD.remove(s_write_tmp);
  s_write_file = SD.open(s_write_tmp, FILE_WRITE);
  if (!s_write_file) {
    Serial.printf("[SD] cannot open %s for write\n", id);
    return false;
  }
  s_write_open = true;
  return true;
}

bool iconStoreWriteChunk(const uint8_t* data, size_t len) {
  if (!s_write_open) {
    return false;
  }
  return s_write_file.write(data, len) == len;
}

bool iconStoreWriteEnd() {
  if (!s_write_open) {
    return false;
  }
  const String tmp = s_write_tmp;
  const String finalPath = s_write_final;
  s_write_file.close();
  s_write_open = false;

  // Validate the freshly written file before committing.
  File f = SD.open(tmp, FILE_READ);
  bool valid = false;
  if (f) {
    uint8_t header[8];
    if (f.size() >= 8 && f.read(header, 8) == 8 && memcmp(header, ICON_MAGIC, 4) == 0) {
      const uint16_t w = header[4] | (header[5] << 8);
      const uint16_t h = header[6] | (header[7] << 8);
      const size_t px_bytes = static_cast<size_t>(w) * h * 2;
      valid = w > 0 && h > 0 && w <= ICON_MAX_DIM && h <= ICON_MAX_DIM && f.size() == 8 + px_bytes;
    }
    f.close();
  }
  if (!valid) {
    SD.remove(tmp);
    Serial.println("[SD] rejected invalid icon upload");
    return false;
  }
  SD.remove(finalPath);
  if (!SD.rename(tmp, finalPath)) {
    SD.remove(tmp);
    return false;
  }
  iconStoreInvalidate();
  Serial.printf("[SD] icon saved %s\n", finalPath.c_str());
  return true;
}

void iconStoreWriteAbort() {
  if (s_write_open) {
    s_write_file.close();
    s_write_open = false;
    SD.remove(s_write_tmp);
  }
}

bool iconStoreDelete(const char* id) {
  if (!s_ready || !validId(id)) {
    return false;
  }
  iconStoreInvalidate();
  return SD.remove(pathFor(id));
}

void iconStoreInvalidate() { freeCache(); }

String iconStoreListJson() {
  String out = "[";
  if (s_ready) {
    File dir = SD.open(ICON_DIR);
    if (dir && dir.isDirectory()) {
      bool first = true;
      for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
        String name = entry.name();
        const int slash = name.lastIndexOf('/');
        if (slash >= 0) {
          name = name.substring(slash + 1);
        }
        entry.close();
        if (!name.endsWith(".bin")) {
          continue;
        }
        name = name.substring(0, name.length() - 4);
        if (!first) {
          out += ",";
        }
        out += "\"" + name + "\"";
        first = false;
      }
    }
    if (dir) {
      dir.close();
    }
  }
  out += "]";
  return out;
}
