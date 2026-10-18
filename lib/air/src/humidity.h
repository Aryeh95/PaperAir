#pragma once
// Relative humidity after a temperature correction.
#include <algorithm>
#include <cmath>

namespace air {

// Saturation vapour pressure in hPa (Magnus formula, Sonntag 1990 constants)
inline float saturation_vapour_pressure(float t_celsius) {
  return 6.112f * std::exp(17.62f * t_celsius / (243.12f + t_celsius));
}

// If the case makes the sensor read warm, correcting only the temperature would leave the
// relative humidity too low. This keeps the amount of water in the air the same and works out
// the relative humidity at the corrected temperature.
inline float compensate_humidity(float rh, float t_raw, float t_offset) {
  if (std::isnan(rh) || std::isnan(t_raw) || t_offset == 0.0f) return rh;
  const float corrected = rh * saturation_vapour_pressure(t_raw) / saturation_vapour_pressure(t_raw + t_offset);
  return std::min(std::max(corrected, 0.0f), 100.0f);
}

}  // namespace air
