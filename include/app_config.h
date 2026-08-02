#pragma once

#include <stdint.h>

// Application defaults (overridable via NVS / portal)
#define APP_DEFAULT_DEVICE_NAME   "TouchDeck"
#define APP_DEFAULT_OTA_PASSWORD  "touchdeck"
#define APP_DEFAULT_AP_PASSWORD   "touchdeck"
#define APP_WIFI_CONNECT_TIMEOUT_MS   15000
#define APP_WIFI_MAX_RETRIES          3
#define APP_WIFI_RETRY_DELAY_MS       3000
#define APP_AP_RECHECK_INTERVAL_MS    60000

#define APP_VOLUME_STEP               3
#define APP_VOLUME_DEFAULT            50
#define APP_VOLUME_MIN                0
#define APP_VOLUME_MAX                100

// Idle display timeouts (seconds). 0 disables that stage.
#define APP_IDLE_DIM_S_DEFAULT        30
#define APP_IDLE_CLOCK_S_DEFAULT      120
#define APP_IDLE_DIM2_S_DEFAULT       300
#define APP_IDLE_OFF_S_DEFAULT        1800  // 30 minutes

#define APP_BRIGHTNESS_FULL           100
#define APP_BRIGHTNESS_DIM_DEFAULT    30
#define APP_BRIGHTNESS_DIM2_DEFAULT   30

// Clock face size. Must match one of the fonts in src/ui/fonts/.
#define APP_CLOCK_FONT_PX_DEFAULT     96

// POSIX TZ for Vietnam (UTC+7). Sign is inverted in POSIX form.
#define APP_TIMEZONE_POSIX            "ICT-7"
#define APP_NTP_SERVER                "pool.ntp.org"

#define APP_SERIAL_BAUD               115200
#define APP_INPUT_QUEUE_LEN           16

#define APP_GRID_DEFAULT_COLS         4
#define APP_GRID_DEFAULT_ROWS         2
