#pragma once
// US EPA Air Quality Index for PM2.5, using the breakpoints revised in 2024.
// https://www.airnow.gov/aqi/aqi-calculator-concentration/
#include <algorithm>
#include <cmath>

namespace air {

struct AqiBreakpoint {
  float c_lo, c_hi;  // ug/m3
  int i_lo, i_hi;    // index
};

inline constexpr AqiBreakpoint kPm25Breakpoints[] = {
    {0.0f, 9.0f, 0, 50},       {9.1f, 35.4f, 51, 100},    {35.5f, 55.4f, 101, 150},
    {55.5f, 125.4f, 151, 200}, {125.5f, 225.4f, 201, 300}, {225.5f, 325.4f, 301, 500},
};

// AQI from a PM2.5 concentration in ug/m3, or -1 for no data. This uses the reading as it is;
// the official AQI averages over hours, so it's an estimate.
inline int aqi_from_pm25(float c) {
  if (std::isnan(c)) return -1;
  c = std::floor(std::max(c, 0.0f) * 10.0f) / 10.0f;  // EPA truncates to 0.1 ug/m3
  for (const auto &b : kPm25Breakpoints) {
    if (c <= b.c_hi) return (int) std::lround((b.i_hi - b.i_lo) / (b.c_hi - b.c_lo) * (c - b.c_lo) + b.i_lo);
  }
  return 500;
}

// PM2.5 in ug/m3 for an AQI value (e.g. one from AirNow), or NAN for no data.
inline float pm25_from_aqi(int aqi) {
  if (aqi < 0) return NAN;
  for (const auto &b : kPm25Breakpoints) {
    if (aqi <= b.i_hi) return (float) (aqi - b.i_lo) / (b.i_hi - b.i_lo) * (b.c_hi - b.c_lo) + b.c_lo;
  }
  return 325.4f;
}

enum class AqiCategory { None, Good, Moderate, Sensitive, Unhealthy, VeryUnhealthy, Hazardous };

inline AqiCategory aqi_category(int aqi) {
  if (aqi < 0) return AqiCategory::None;
  if (aqi <= 50) return AqiCategory::Good;
  if (aqi <= 100) return AqiCategory::Moderate;
  if (aqi <= 150) return AqiCategory::Sensitive;
  if (aqi <= 200) return AqiCategory::Unhealthy;
  if (aqi <= 300) return AqiCategory::VeryUnhealthy;
  return AqiCategory::Hazardous;
}

// Short everyday word for the category, as shown on screen
inline const char *aqi_word(int aqi) {
  switch (aqi_category(aqi)) {
    case AqiCategory::Good: return "clean";
    case AqiCategory::Moderate: return "moderate";
    case AqiCategory::Sensitive: return "hazy";
    case AqiCategory::Unhealthy: return "smoky";
    case AqiCategory::VeryUnhealthy: return "very smoky";
    case AqiCategory::Hazardous: return "hazardous";
    default: return "";
  }
}

}  // namespace air
