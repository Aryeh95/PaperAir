#include "outdoor.h"

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "airnow.h"
#include "aqi.h"
#include "purpleair.h"
#include "settings.h"

// The certificate authorities built into the ESP32 Arduino core, to check who we're talking to
extern const uint8_t ca_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t ca_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace {

struct OutdoorConfig {
  String purpleair_key;
  int purpleair_index = 0, purpleair_backup = 0;
  String airnow_key, airnow_zip;
};

SemaphoreHandle_t mtx = nullptr;
TaskHandle_t worker = nullptr;
OutdoorConfig pending_cfg;
air::Outdoor result;
bool busy = false, finished = false;

// Fetches a URL; returns the body, or "" with `error` set
String get(const String &url, const char *header_name, const char *header_value, String &error) {
  NetworkClientSecure client;
  client.setCACertBundle(ca_bundle_start, ca_bundle_end - ca_bundle_start);
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    error = "bad address";
    return "";
  }
  if (header_name) http.addHeader(header_name, header_value);
  const int code = http.GET();
  String body;
  if (code == HTTP_CODE_OK) {
    body = http.getString();
  } else {
    error = code < 0 ? http.errorToString(code) : "HTTP " + String(code);
  }
  http.end();
  return body;
}

bool from_purpleair(const String &key, long index, bool backup, air::Outdoor &out) {
  if (!key.length() || index <= 0) return false;
  const String url = "https://api.purpleair.com/v1/sensors/" + String(index) +
                     "?fields=name,location_type,last_seen,humidity,pm2.5_cf_1,pm2.5_cf_1_a,pm2.5_cf_1_b";
  String error;
  const String body = get(url, "X-API-Key", key.c_str(), error);
  if (body.isEmpty()) {
    Serial.printf("PurpleAir %ld: %s\n", index, error.c_str());
    return false;
  }
  const air::PurpleAirReading r = air::parse_purpleair(body.c_str(), body.length());
  if (!r.ok) {
    Serial.printf("PurpleAir %ld: %s\n", index, r.error.c_str());
    return false;
  }
  std::string src = r.name.empty() ? (backup ? "PurpleAir (backup)" : "PurpleAir") : "PurpleAir · " + r.name;
  out = {true, air::aqi_from_pm25(r.pm25), r.pm25, src};
  Serial.printf("PurpleAir %ld: PM2.5 %.1f (corrected), AQI %d\n", index, r.pm25, out.aqi);
  return true;
}

bool from_airnow(const String &key, const String &zip, air::Outdoor &out) {
  if (!key.length() || !zip.length()) return false;
  const String url = String("https://www.airnowapi.org/aq/observation/current/ziplatlong/?format=application/json&zipCode=") +
                     zip + "&distance=25&API_KEY=" + key;
  String error;
  const String body = get(url, nullptr, nullptr, error);
  if (body.isEmpty()) {
    Serial.printf("AirNow: %s\n", error.c_str());
    return false;
  }
  const air::AirNowReading r = air::parse_airnow(body.c_str(), body.length());
  if (!r.ok) {
    Serial.printf("AirNow: %s\n", r.error.c_str());
    return false;
  }
  // Without a PM2.5 monitor nearby, pm25 stays NAN and the advice uses the overall AQI
  out = {true, r.aqi, r.pm25_aqi >= 0 ? air::pm25_from_aqi(r.pm25_aqi) : NAN, "AirNow"};
  Serial.printf("AirNow: AQI %d (PM2.5 AQI %d)\n", r.aqi, r.pm25_aqi);
  return true;
}

air::Outdoor fetch_all(const OutdoorConfig &cfg) {
  air::Outdoor out;
  if (from_purpleair(cfg.purpleair_key, cfg.purpleair_index, false, out)) return out;
  if (from_purpleair(cfg.purpleair_key, cfg.purpleair_backup, true, out)) return out;
  if (from_airnow(cfg.airnow_key, cfg.airnow_zip, out)) return out;
  return air::Outdoor();
}

void worker_task(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    xSemaphoreTake(mtx, portMAX_DELAY);
    OutdoorConfig cfg = pending_cfg;
    xSemaphoreGive(mtx);

    air::Outdoor out = fetch_all(cfg);

    xSemaphoreTake(mtx, portMAX_DELAY);
    result = std::move(out);
    busy = false;
    finished = true;
    xSemaphoreGive(mtx);
  }
}

}  // namespace

void outdoor_start() {
  if (!mtx) {
    mtx = xSemaphoreCreateMutex();
    if (!mtx) return;
  }
  if (!worker) {
    xTaskCreatePinnedToCore(worker_task, "outdoor", 16384, nullptr, 1, &worker, 0);
    if (!worker) return;
  }
  OutdoorConfig cfg{settings.purpleair_key, settings.purpleair_index, settings.purpleair_backup, settings.airnow_key,
                    settings.airnow_zip};
  bool started = false;
  xSemaphoreTake(mtx, portMAX_DELAY);
  if (!busy) {
    pending_cfg = std::move(cfg);
    busy = true;
    finished = false;
    started = true;
  }
  xSemaphoreGive(mtx);
  if (started) xTaskNotifyGive(worker);
}

bool outdoor_busy() {
  if (!mtx) return false;
  xSemaphoreTake(mtx, portMAX_DELAY);
  const bool b = busy;
  xSemaphoreGive(mtx);
  return b;
}

bool outdoor_poll(air::Outdoor &out) {
  if (!mtx) return false;
  bool done = false;
  xSemaphoreTake(mtx, portMAX_DELAY);
  if (finished) {
    out = std::move(result);
    finished = false;
    done = true;
  }
  xSemaphoreGive(mtx);
  return done;
}
