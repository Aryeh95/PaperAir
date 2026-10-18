#pragma once
// Outdoor air quality from AirNow (US government monitors), the fallback when PurpleAir has no
// recent reading.
// Service: "Current Observations by Zip Code or Lat/Long", which replaced the zipCode/latLong
// observation services that AirNow retired on 1 October 2026:
//   https://www.airnowapi.org/aq/observation/current/ziplatlong/?format=application/json&zipCode=ZIP&distance=25&API_KEY=KEY
// The response is an array with one entry per pollutant (OZONE, PM2.5, PM10) from the closest
// monitor for each, with a NowCast AQI. When there's nothing to report it's an error object.
#include <ArduinoJson.h>
#include <algorithm>
#include <cstring>
#include <string>

namespace air {

struct AirNowReading {
  bool ok = false;
  int aqi = -1;       // overall: the highest of the pollutants
  int pm25_aqi = -1;  // -1 if no monitor nearby reports PM2.5
  std::string error;
};

inline AirNowReading parse_airnow(const char *body, size_t len) {
  AirNowReading r;
  JsonDocument doc;
  if (deserializeJson(doc, body, len)) {
    r.error = "unreadable response";
    return r;
  }
  JsonArray observations = doc.as<JsonArray>();
  if (observations.isNull()) {
    r.error = doc["WebServiceError"][0]["Message"] | "unexpected response";
    return r;
  }
  for (JsonObject o : observations) {
    const int aqi = o["nowcastAQI"] | (o["AQI"] | -1);  // "AQI" in the retired service
    const char *parameter = o["parameterName"] | (o["ParameterName"] | "");
    if (aqi < 0) continue;
    r.ok = true;
    r.aqi = std::max(r.aqi, aqi);
    if (std::strcmp(parameter, "PM2.5") == 0) r.pm25_aqi = aqi;
  }
  if (!r.ok) r.error = "no observations nearby";
  return r;
}

}  // namespace air
