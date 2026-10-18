#include "outdoor.h"

#include <HTTPClient.h>
#include <NetworkClientSecure.h>

#include "airnow.h"
#include "aqi.h"
#include "purpleair.h"
#include "secrets.h"

// The certificate authorities built into the ESP32 Arduino core, to check who we're talking to
extern const uint8_t ca_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t ca_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace {

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

bool from_purpleair(long index, air::Outdoor &out) {
  if (!strlen(PURPLEAIR_API_KEY) || index <= 0) return false;
  const String url = "https://api.purpleair.com/v1/sensors/" + String(index) +
                     "?fields=name,location_type,last_seen,humidity,pm2.5_cf_1,pm2.5_cf_1_a,pm2.5_cf_1_b";
  String error;
  const String body = get(url, "X-API-Key", PURPLEAIR_API_KEY, error);
  if (body.isEmpty()) {
    Serial.printf("PurpleAir %ld: %s\n", index, error.c_str());
    return false;
  }
  const air::PurpleAirReading r = air::parse_purpleair(body.c_str(), body.length());
  if (!r.ok) {
    Serial.printf("PurpleAir %ld: %s\n", index, r.error.c_str());
    return false;
  }
  out = {true, air::aqi_from_pm25(r.pm25), r.pm25};
  Serial.printf("PurpleAir %ld: PM2.5 %.1f (corrected), AQI %d\n", index, r.pm25, out.aqi);
  return true;
}

bool from_airnow(air::Outdoor &out) {
  if (!strlen(AIRNOW_API_KEY) || !strlen(AIRNOW_ZIP_CODE)) return false;
  const String url = String("https://www.airnowapi.org/aq/observation/current/ziplatlong/?format=application/json&zipCode=") +
                     AIRNOW_ZIP_CODE + "&distance=25&API_KEY=" + AIRNOW_API_KEY;
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
  // The advice needs PM2.5; without a PM2.5 monitor nearby only the AQI is shown
  out = {true, r.aqi, r.pm25_aqi >= 0 ? air::pm25_from_aqi(r.pm25_aqi) : NAN};
  Serial.printf("AirNow: AQI %d (PM2.5 AQI %d)\n", r.aqi, r.pm25_aqi);
  return true;
}

}  // namespace

air::Outdoor fetch_outdoor() {
  air::Outdoor out;
  if (from_purpleair(PURPLEAIR_SENSOR_INDEX, out)) return out;
  if (from_purpleair(PURPLEAIR_BACKUP_SENSOR_INDEX, out)) return out;
  if (from_airnow(out)) return out;
  return air::Outdoor();
}
