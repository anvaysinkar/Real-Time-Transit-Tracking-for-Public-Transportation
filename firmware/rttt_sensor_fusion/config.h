// config.h -- RTTT sensor-fusion node: pins, timing and network settings.
// Edit this file for your wiring and network; the sketch itself should not need changes.
#pragma once

// ---------------- identity ----------------
#define BUS_ID              "PMPML_45"

// ---------------- pins (ESP32-WROOM-32D) ----------------
#define PIN_MAT             34      // pressure mat voltage divider -> ADC1 (input-only pin)
#define PIN_IR_OUTER        26      // IR break-beam receiver nearer the outside of the bus
#define PIN_IR_INNER        27      // IR break-beam receiver nearer the inside of the bus
#define IR_ACTIVE_LEVEL     LOW     // level the receiver outputs while its beam is broken
#define PIN_GPS_RX          16      // ESP32 RX  <- NEO-6M TX
#define PIN_GPS_TX          17      // ESP32 TX  -> NEO-6M RX
#define GPS_BAUD            9600

// ---------------- pressure mat ----------------
// The mat reads HIGH when empty and drops as weight goes on (Velostat resistance falls).
// Defaults match the single-zone prototype; run the one-time calibration ('c' over serial)
// to replace them with values measured on your mat. Calibration is saved in flash.
#define MAT_DEFAULT_IDLE    2500    // filtered ADC with nobody on the mat
#define MAT_DEFAULT_ONE     500     // filtered ADC with one adult standing on the mat
#define MAT_AVG_N           8       // moving-average length (samples at 100 Hz)
#define CROWD_FRACTION      0.5f    // crowded if the reading falls below this fraction of the
                                    // one-person level (more weight -> lower Velostat resistance)

// ---------------- timing ----------------
#define SAMPLE_PERIOD_MS    10      // 100 Hz sensor loop
#define IR_DEBOUNCE_MS      30      // an IR input must hold a new level this long to count
#define EVENT_WINDOW_MS     1500    // first IR -> mat -> second IR must all happen inside this window

// ---------------- network ----------------
// Local dashboard: the node always runs its own access point.
#define AP_SSID             "RTTT-" BUS_ID
#define AP_PASS             "rttt1234"           // at least 8 characters
// Backend upload (optional). Leave WIFI_SSID empty to run with the local dashboard only.
#define WIFI_SSID           ""
#define WIFI_PASS           ""
#define BACKEND_URL         ""                    // e.g. "https://example.com/api/events" (HTTP POST, JSON body)
#define NTP_SERVER          "pool.ntp.org"        // used for timestamps until GPS has a fix
#define EVENT_QUEUE_LEN     64                    // events kept for upload while offline
#define EVENT_LOG_LEN       50                    // recent events shown on the dashboard
