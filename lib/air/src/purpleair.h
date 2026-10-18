#pragma once
// Outdoor PM2.5 from a PurpleAir sensor: the US EPA correction, its quality check, and parsing
// the API response.
// API: https://api.purpleair.com/#api-sensors-get-sensor-data
// Request these fields: name, location_type, last_seen, humidity, pm2.5_cf_1, pm2.5_cf_1_a, pm2.5_cf_1_b
#include <ArduinoJson.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace air {

// US EPA correction for PurpleAir sensors (Barkjohn et al. 2021, extended for smoke as on the
// AirNow Fire and Smoke Map). cf1 is the "CF=1" PM2.5 averaged over channels A and B (ug/m3),
// rh the sensor's own humidity reading (%).
inline float epa_correct_purpleair(float cf1, float rh) {
  if (std::isnan(cf1) || std::isnan(rh)) return NAN;
  const float x = std::max(cf1, 0.0f);
  float pm;
  if (x < 30) {
    pm = 0.524f * x - 0.0862f * rh + 5.75f;
  } else if (x < 50) {
    const float w = x / 20 - 1.5f;
    pm = (0.786f * w + 0.524f * (1 - w)) * x - 0.0862f * rh + 5.75f;
  } else if (x < 210) {
    pm = 0.786f * x - 0.0862f * rh + 5.75f;
  } else if (x < 260) {
    const float w = x / 50 - 4.2f;
    pm = (0.69f * w + 0.786f * (1 - w)) * x - 0.0862f * rh * (1 - w) + 2.966f * w + 5.75f * (1 - w) +
         8.84e-4f * x * x * w;
  } else {
    pm = 2.966f + 0.69f * x + 8.84e-4f * x * x;
  }
  return std::max(pm, 0.0f);
}

// EPA quality check: the two laser counters must roughly agree (not more than 5 ug/m3 AND 70% apart)
inline bool purpleair_channels_agree(float a, float b) {
  if (std::isnan(a) || std::isnan(b)) return false;
  const float diff = std::fabs(a - b);
  const float mean = (a + b) / 2;
  return !(diff > 5.0f && mean > 0 && diff / mean > 0.7f);
}

struct PurpleAirReading {
  bool ok = false;
  float pm25 = NAN;     // EPA-corrected, ug/m3
  float raw_cf1 = NAN;  // average of channels A and B
  float humidity = NAN;
  bool humidity_assumed = false;  // the sensor reports no humidity, so 50% was used
  std::string name;
  std::string error;
};

inline PurpleAirReading parse_purpleair(const char *body, size_t len) {
  PurpleAirReading r;
  JsonDocument doc;
  if (deserializeJson(doc, body, len)) {
    r.error = "unreadable response";
    return r;
  }
  JsonObject s = doc["sensor"];
  if (s.isNull()) {
    const char *why = doc["description"] | (doc["error"] | "no sensor data");
    r.error = why;
    return r;
  }
  r.name = s["name"] | "";
  if ((s["location_type"] | 0) != 0) {
    r.error = "sensor is marked as indoor";
    return r;
  }
  const uint32_t data_time = doc["data_time_stamp"] | 0u;
  const uint32_t last_seen = s["last_seen"] | 0u;
  if (data_time && last_seen && data_time > last_seen && data_time - last_seen > 30 * 60) {
    r.error = "sensor hasn't reported for over 30 minutes";
    return r;
  }
  const float a = s["pm2.5_cf_1_a"] | NAN;
  const float b = s["pm2.5_cf_1_b"] | NAN;
  if (!std::isnan(a) && !std::isnan(b)) {
    if (!purpleair_channels_agree(a, b)) {
      r.error = "channels A and B disagree";
      return r;
    }
    r.raw_cf1 = (a + b) / 2;
  } else {
    r.raw_cf1 = s["pm2.5_cf_1"] | NAN;  // single-channel sensor
  }
  r.humidity = s["humidity"] | NAN;
  if (std::isnan(r.humidity)) {
    // The humidity term is small (about 0.9 ug/m3 per 10% RH), so a typical value beats no reading
    r.humidity = 50;
    r.humidity_assumed = true;
  }
  r.pm25 = epa_correct_purpleair(r.raw_cf1, r.humidity);
  if (std::isnan(r.pm25)) {
    r.error = "no PM2.5 in the response";
    return r;
  }
  r.ok = true;
  return r;
}

}  // namespace air
