/*
 * rttt_sensor_fusion.ino -- RTTT passenger counter: pressure mat + 2 IR break-beams + GPS on an ESP32.
 *
 * Implements the on-device logic of Section III of the paper
 * (G. Ghuge, A. Sinkar, S. Patil, S. Sagar, "Real Time Transit Tracking for Public Transportation",
 *  IEEE INDICON 2025, doi:10.1109/INDICON68490.2025.11392965).
 *
 * Direction from the order of the beams:
 *     boarding  = IR_OUTER -> mat -> IR_INNER   within EVENT_WINDOW_MS
 *     alighting = IR_INNER -> mat -> IR_OUTER   within EVENT_WINDOW_MS
 * State machine: IDLE -> AWAITING_PRESSURE -> AWAITING_SECOND_IR -> EVENT_COMPLETE -> IDLE.
 * Only a complete sequence is counted, so luggage that presses the mat without breaking both beams
 * in order, and passengers who step in and turn back, are ignored.
 * Filtering: IR inputs are debounced; the mat ADC goes through a moving average and is compared with a
 * threshold from a one-time calibration (saved in flash).
 * Lingering: after an event, nothing new is counted until the mat is released and both beams are clear.
 * Crowding: two or three people on the mat at once push the reading well below the one-person level;
 * the event is then sent with is_crowded_event = true (it still counts as one, as in the paper).
 *
 * Every event becomes a JSON payload:
 *   {"timestamp":"2025-05-07T13:45:08Z","bus_id":"PMPML_45","latitude":18.5,"longitude":73.8591,
 *    "event_type":"boarding","current_occupancy":34,"is_crowded_event":false}
 * It is queued for HTTP POST to BACKEND_URL (if configured) and shown on a local dashboard served from
 * the node's own Wi-Fi access point, where the event log can also be downloaded as a text file.
 *
 * Libraries: TinyGPSPlus (Mikal Hart). Everything else ships with the ESP32 Arduino core.
 * Serial (115200): i = record empty-mat level, o = record one-person level, s = status, r = reset counts.
 */
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <TinyGPS++.h>
#include <time.h>
#include "config.h"

// ---------------------------------------------------------------- types
enum FusionState { IDLE, AWAITING_PRESSURE, AWAITING_SECOND_IR, EVENT_COMPLETE };
enum Direction   { DIR_NONE, DIR_BOARDING, DIR_ALIGHTING };

struct PassengerEvent {
  char     timestamp[25];   // ISO 8601 UTC, empty if no time source yet
  uint32_t uptimeMs;
  bool     hasFix;
  double   lat, lon;
  bool     boarding;
  int32_t  occupancy;
  bool     crowded;
};

struct DebouncedInput {
  uint8_t  pin;
  bool     state = false;        // true = beam broken (stable)
  bool     raw = false;
  uint32_t rawSince = 0;
  bool     rose = false;         // became broken this sample
  void begin(uint8_t p) { pin = p; pinMode(pin, INPUT_PULLUP); }
  void update(uint32_t now) {
    rose = false;
    bool r = (digitalRead(pin) == IR_ACTIVE_LEVEL);
    if (r != raw) { raw = r; rawSince = now; }
    if (raw != state && now - rawSince >= IR_DEBOUNCE_MS) { state = raw; rose = state; }
  }
};

// ---------------------------------------------------------------- globals
TinyGPSPlus    gps;
HardwareSerial gpsSerial(2);
WebServer      server(80);
Preferences    prefs;
QueueHandle_t  uploadQueue;

DebouncedInput irOuter, irInner;

uint16_t matBuf[MAT_AVG_N];
uint32_t matSum = 0;
uint8_t  matIdx = 0;
float    matFiltered = 0;
float    matIdle, matOne;            // calibration
float    pressLevel, releaseLevel, crowdLevel;
bool     matPressed = false;

FusionState state = IDLE;
Direction   dir   = DIR_NONE;
uint32_t    seqStart = 0;
float       seqMinMat = 4095;

int32_t  occupancy = 0;
uint32_t boardings = 0, alightings = 0, crowdedEvents = 0, rejectedSequences = 0;

PassengerEvent eventLog[EVENT_LOG_LEN];
uint16_t logHead = 0, logCount = 0;
uint32_t uploadedOk = 0, uploadFailed = 0, droppedOffline = 0;

// ---------------------------------------------------------------- mat
void recomputeLevels() {
  float span   = matIdle - matOne;                // positive: the mat reads lower under load
  pressLevel   = matOne + 0.5f * span;            // halfway between empty and one person
  releaseLevel = matOne + 0.7f * span;            // hysteresis so a shifting foot is not a new step
  crowdLevel   = matOne * CROWD_FRACTION;          // 2-3 people press well past one person
}

void loadCalibration() {
  prefs.begin("rttt", true);
  matIdle = prefs.getFloat("idle", MAT_DEFAULT_IDLE);
  matOne  = prefs.getFloat("one",  MAT_DEFAULT_ONE);
  prefs.end();
  recomputeLevels();
}

void saveCalibration() {
  prefs.begin("rttt", false);
  prefs.putFloat("idle", matIdle);
  prefs.putFloat("one", matOne);
  prefs.end();
  recomputeLevels();
}

void sampleMat() {
  uint16_t v = analogRead(PIN_MAT);
  matSum += v - matBuf[matIdx];
  matBuf[matIdx] = v;
  matIdx = (matIdx + 1) % MAT_AVG_N;
  matFiltered = (float)matSum / MAT_AVG_N;
  if (!matPressed && matFiltered < pressLevel)        matPressed = true;
  else if (matPressed && matFiltered > releaseLevel)  matPressed = false;
}

float averageMat(uint32_t ms) {                       // blocking, used only for calibration
  uint32_t t0 = millis(); double s = 0; uint32_t n = 0;
  while (millis() - t0 < ms) { s += analogRead(PIN_MAT); n++; delay(SAMPLE_PERIOD_MS); }
  return n ? s / n : 0;
}

// ---------------------------------------------------------------- time and position
void formatTimestamp(char* out, size_t len) {
  out[0] = 0;
  if (gps.date.isValid() && gps.time.isValid() && gps.time.age() < 2000 && gps.date.year() >= 2024) {
    snprintf(out, len, "%04d-%02d-%02dT%02d:%02d:%02dZ", gps.date.year(), gps.date.month(), gps.date.day(),
             gps.time.hour(), gps.time.minute(), gps.time.second());
    return;
  }
  time_t now = time(nullptr);
  if (now > 1700000000) {                             // NTP has synced
    struct tm t; gmtime_r(&now, &t);
    strftime(out, len, "%Y-%m-%dT%H:%M:%SZ", &t);
  }
}

// ---------------------------------------------------------------- events
void eventToJson(const PassengerEvent& e, char* out, size_t len) {
  char ts[32], la[16], lo[16];
  if (e.timestamp[0]) snprintf(ts, sizeof ts, "\"%s\"", e.timestamp); else strcpy(ts, "null");
  if (e.hasFix) { snprintf(la, sizeof la, "%.6f", e.lat); snprintf(lo, sizeof lo, "%.6f", e.lon); }
  else          { strcpy(la, "null"); strcpy(lo, "null"); }
  snprintf(out, len,
           "{\"timestamp\":%s,\"bus_id\":\"%s\",\"latitude\":%s,\"longitude\":%s,"
           "\"event_type\":\"%s\",\"current_occupancy\":%ld,\"is_crowded_event\":%s,\"uptime_ms\":%lu}",
           ts, BUS_ID, la, lo, e.boarding ? "boarding" : "alighting", (long)e.occupancy,
           e.crowded ? "true" : "false", (unsigned long)e.uptimeMs);
}

void logEvent(bool boarding, bool crowded) {
  if (boarding) { occupancy++; boardings++; }
  else          { if (occupancy > 0) occupancy--; alightings++; }
  if (crowded) crowdedEvents++;

  PassengerEvent e;
  formatTimestamp(e.timestamp, sizeof e.timestamp);
  e.uptimeMs  = millis();
  e.hasFix    = gps.location.isValid() && gps.location.age() < 5000;
  e.lat       = e.hasFix ? gps.location.lat() : 0;
  e.lon       = e.hasFix ? gps.location.lng() : 0;
  e.boarding  = boarding;
  e.occupancy = occupancy;
  e.crowded   = crowded;

  eventLog[logHead] = e;
  logHead = (logHead + 1) % EVENT_LOG_LEN;
  if (logCount < EVENT_LOG_LEN) logCount++;

  if (strlen(BACKEND_URL) > 0) {
    if (xQueueSend(uploadQueue, &e, 0) != pdTRUE) {   // full: drop the oldest, keep the newest
      PassengerEvent old; xQueueReceive(uploadQueue, &old, 0); droppedOffline++;
      xQueueSend(uploadQueue, &e, 0);
    }
  }

  char json[256]; eventToJson(e, json, sizeof json);
  Serial.println(json);
}

// ---------------------------------------------------------------- sensor fusion state machine
void runFusion(uint32_t now) {
  switch (state) {
    case IDLE:
      if (irOuter.rose || irInner.rose) {
        dir = irOuter.rose ? DIR_BOARDING : DIR_ALIGHTING;
        seqStart = now; seqMinMat = matFiltered;
        state = AWAITING_PRESSURE;
      }
      break;

    case AWAITING_PRESSURE:
      if (matFiltered < seqMinMat) seqMinMat = matFiltered;
      if (matPressed) state = AWAITING_SECOND_IR;
      else if (now - seqStart > EVENT_WINDOW_MS) { rejectedSequences++; state = IDLE; }
      break;

    case AWAITING_SECOND_IR: {
      if (matFiltered < seqMinMat) seqMinMat = matFiltered;
      bool secondBeam = (dir == DIR_BOARDING) ? irInner.rose : irOuter.rose;
      if (secondBeam) {
        logEvent(dir == DIR_BOARDING, seqMinMat < crowdLevel);
        state = EVENT_COMPLETE;
      } else if (now - seqStart > EVENT_WINDOW_MS) {
        rejectedSequences++;                       // turned back, or luggage on the mat only
        state = IDLE;
      }
      break;
    }

    case EVENT_COMPLETE:                           // one count per crossing, however long they stand
      if (!matPressed && !irOuter.state && !irInner.state) { dir = DIR_NONE; state = IDLE; }
      break;
  }
}

// ---------------------------------------------------------------- upload task (core 0)
void uploadTask(void*) {
  PassengerEvent e;
  char json[256];
  for (;;) {
    if (xQueuePeek(uploadQueue, &e, pdMS_TO_TICKS(1000)) != pdTRUE) continue;
    if (WiFi.status() != WL_CONNECTED) { vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
    eventToJson(e, json, sizeof json);
    HTTPClient http;
    http.setTimeout(5000);
    bool ok = false;
    if (http.begin(BACKEND_URL)) {
      http.addHeader("Content-Type", "application/json");
      int code = http.POST((uint8_t*)json, strlen(json));
      ok = code >= 200 && code < 300;
      http.end();
    }
    if (ok) { xQueueReceive(uploadQueue, &e, 0); uploadedOk++; }
    else    { uploadFailed++; vTaskDelay(pdMS_TO_TICKS(5000)); }
  }
}

// ---------------------------------------------------------------- local dashboard
const char PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>RTTT</title>
<style>body{font-family:system-ui,Arial;margin:16px;background:#f4f5f7}h1{font-size:20px}
.g{display:flex;gap:10px;flex-wrap:wrap}.c{background:#fff;border-radius:8px;padding:12px 16px;min-width:120px}
.v{font-size:28px;font-weight:600}table{border-collapse:collapse;width:100%;background:#fff;margin-top:12px}
td,th{padding:6px 8px;border-bottom:1px solid #eee;font-size:13px;text-align:left}</style></head><body>
<h1>RTTT &middot; <span id="bus"></span></h1>
<div class="g"><div class="c">Occupancy<div class="v" id="occ">-</div></div>
<div class="c">Boarded<div class="v" id="b">-</div></div><div class="c">Alighted<div class="v" id="a">-</div></div>
<div class="c">Crowded events<div class="v" id="cr">-</div></div></div>
<p id="pos"></p><p><a href="/download">Download event log (.txt)</a> &middot; <a href="#" onclick="fetch('/reset').then(u)">Reset counts</a></p>
<table><thead><tr><th>Time</th><th>Event</th><th>Occupancy</th><th>Location</th><th>Crowded</th></tr></thead><tbody id="ev"></tbody></table>
<script>
function u(){fetch('/status').then(r=>r.json()).then(s=>{bus.textContent=s.bus_id;occ.textContent=s.occupancy;
b.textContent=s.boardings;a.textContent=s.alightings;cr.textContent=s.crowded_events;
pos.textContent=s.gps_fix?('Position '+s.latitude.toFixed(5)+', '+s.longitude.toFixed(5)):'Waiting for GPS fix';});
fetch('/events').then(r=>r.json()).then(l=>{ev.innerHTML=l.reverse().map(e=>'<tr><td>'+(e.timestamp||('+'+(e.uptime_ms/1000).toFixed(1)+' s'))+
'</td><td>'+e.event_type+'</td><td>'+e.current_occupancy+'</td><td>'+(e.latitude===null?'-':e.latitude.toFixed(5)+', '+e.longitude.toFixed(5))+
'</td><td>'+(e.is_crowded_event?'yes':'')+'</td></tr>').join('');});}
u();setInterval(u,1000);
</script></body></html>)HTML";

void handleStatus() {
  bool fix = gps.location.isValid() && gps.location.age() < 5000;
  char out[400];
  snprintf(out, sizeof out,
           "{\"bus_id\":\"%s\",\"occupancy\":%ld,\"boardings\":%lu,\"alightings\":%lu,\"crowded_events\":%lu,"
           "\"rejected_sequences\":%lu,\"gps_fix\":%s,\"latitude\":%.6f,\"longitude\":%.6f,\"mat\":%.0f,"
           "\"state\":%d,\"queued\":%u,\"uploaded\":%lu,\"upload_failed\":%lu}",
           BUS_ID, (long)occupancy, (unsigned long)boardings, (unsigned long)alightings,
           (unsigned long)crowdedEvents, (unsigned long)rejectedSequences, fix ? "true" : "false",
           fix ? gps.location.lat() : 0.0, fix ? gps.location.lng() : 0.0, matFiltered, (int)state,
           (unsigned)uxQueueMessagesWaiting(uploadQueue), (unsigned long)uploadedOk, (unsigned long)uploadFailed);
  server.send(200, "application/json", out);
}

void handleEvents() {
  String s = "[";
  char json[256];
  for (uint16_t i = 0; i < logCount; i++) {
    uint16_t k = (logHead + EVENT_LOG_LEN - logCount + i) % EVENT_LOG_LEN;
    eventToJson(eventLog[k], json, sizeof json);
    if (i) s += ",";
    s += json;
  }
  s += "]";
  server.send(200, "application/json", s);
}

void handleDownload() {
  String s = "RTTT event log, bus " BUS_ID "\n";
  char json[256];
  for (uint16_t i = 0; i < logCount; i++) {
    uint16_t k = (logHead + EVENT_LOG_LEN - logCount + i) % EVENT_LOG_LEN;
    eventToJson(eventLog[k], json, sizeof json);
    s += json; s += "\n";
  }
  server.sendHeader("Content-Disposition", "attachment; filename=passenger_data.txt");
  server.send(200, "text/plain", s);
}

void resetCounts() { occupancy = 0; boardings = alightings = crowdedEvents = rejectedSequences = 0; logCount = 0; }

// ---------------------------------------------------------------- serial commands
void printStatus() {
  Serial.printf("mat %.0f (idle %.0f, one %.0f, press<%.0f, release>%.0f, crowd<%.0f) pressed=%d  "
                "IR outer=%d inner=%d  state=%d  occ=%ld  rejected=%lu  gps sats=%lu fix=%d\n",
                matFiltered, matIdle, matOne, pressLevel, releaseLevel, crowdLevel, matPressed,
                irOuter.state, irInner.state, (int)state, (long)occupancy, (unsigned long)rejectedSequences,
                (unsigned long)gps.satellites.value(), gps.location.isValid());
}

void handleSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'i') {
    Serial.println("Recording EMPTY mat for 2 s...");
    matIdle = averageMat(2000); saveCalibration();
    Serial.printf("idle = %.0f saved\n", matIdle);
  } else if (c == 'o') {
    Serial.println("Recording ONE person standing on the mat for 2 s...");
    matOne = averageMat(2000); saveCalibration();
    Serial.printf("one-person = %.0f saved\n", matOne);
  } else if (c == 's') {
    printStatus();
  } else if (c == 'r') {
    resetCounts(); Serial.println("counts reset");
  }
}

// ---------------------------------------------------------------- setup / loop
void setup() {
  Serial.begin(115200);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  irOuter.begin(PIN_IR_OUTER);
  irInner.begin(PIN_IR_INNER);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_MAT, ADC_11db);

  loadCalibration();
  for (int i = 0; i < MAT_AVG_N; i++) { matBuf[i] = analogRead(PIN_MAT); matSum += matBuf[i]; delay(2); }

  bool useSta = strlen(WIFI_SSID) > 0;
  WiFi.mode(useSta ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  if (useSta) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    WiFi.setAutoReconnect(true);
    configTime(0, 0, NTP_SERVER);
  }

  server.on("/",         [] { server.send_P(200, "text/html", PAGE); });
  server.on("/status",   handleStatus);
  server.on("/events",   handleEvents);
  server.on("/download", handleDownload);
  server.on("/reset",    [] { resetCounts(); server.send(200, "text/plain", "ok"); });
  server.begin();

  uploadQueue = xQueueCreate(EVENT_QUEUE_LEN, sizeof(PassengerEvent));
  xTaskCreatePinnedToCore(uploadTask, "upload", 6144, nullptr, 1, nullptr, 0);

  Serial.printf("RTTT %s ready. Dashboard: connect to Wi-Fi '%s' and open http://%s\n",
                BUS_ID, AP_SSID, WiFi.softAPIP().toString().c_str());
  Serial.println("Keys: i = empty-mat level, o = one-person level, s = status, r = reset counts");
  printStatus();
}

void loop() {
  static uint32_t next = 0;
  uint32_t now = millis();

  while (gpsSerial.available()) gps.encode(gpsSerial.read());
  server.handleClient();
  handleSerial();

  if ((int32_t)(now - next) < 0) return;
  next = now + SAMPLE_PERIOD_MS;

  sampleMat();
  irOuter.update(now);
  irInner.update(now);
  runFusion(now);
}
