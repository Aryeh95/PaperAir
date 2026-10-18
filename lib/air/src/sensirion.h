#pragma once
// The parts of the Sensirion I2C protocol that don't touch the bus, so they can be tested here:
// the checksum, and turning the SEN66's and SFA40's replies into readings.
//
// Every 16-bit word on the wire is followed by a CRC-8 (polynomial 0x31, start 0xFF).
//
// SEN66 "read measured values" (command 0x0300) returns 9 words:
//   PM1, PM2.5, PM4, PM10   unsigned, ug/m3 x 10      (0xFFFF = no value yet)
//   humidity                signed, % x 100           (0x7FFF = no value yet)
//   temperature             signed, degrees C x 200   (0x7FFF)
//   VOC index, NOx index    signed, x 10              (0x7FFF)
//   CO2                     unsigned, ppm             (0xFFFF)
// SFA40 "read measured values" (command 0xC0EB) returns 4 words:
//   formaldehyde ppb x 10, humidity and temperature as raw ticks, and a status byte
//   (bit 0: still warming up, bit 1: outside its specification).
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "readings.h"

namespace air {

inline uint8_t sensirion_crc(const uint8_t *data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) crc = (crc & 0x80) ? (uint8_t) ((crc << 1) ^ 0x31) : (uint8_t) (crc << 1);
  }
  return crc;
}

// Unpacks `words` 16-bit values from a reply of words * 3 bytes. False if any checksum is wrong.
inline bool sensirion_unpack(const uint8_t *bytes, size_t words, uint16_t *out) {
  for (size_t i = 0; i < words; i++) {
    const uint8_t *w = bytes + i * 3;
    if (sensirion_crc(w, 2) != w[2]) return false;
    out[i] = (uint16_t) (w[0] << 8 | w[1]);
  }
  return true;
}

// Packs words into bytes with their checksums; `out` needs words * 3 bytes
inline void sensirion_pack(const uint16_t *words, size_t n, uint8_t *out) {
  for (size_t i = 0; i < n; i++) {
    out[i * 3] = words[i] >> 8;
    out[i * 3 + 1] = words[i] & 0xFF;
    out[i * 3 + 2] = sensirion_crc(out + i * 3, 2);
  }
}

struct Sen66Values {
  float pm1, pm25, pm4, pm10, humidity, temperature, voc, nox, co2;  // NAN = no value yet
};

inline Sen66Values decode_sen66(const uint16_t w[9]) {
  auto pm = [](uint16_t v) { return v == 0xFFFF ? NAN : v / 10.0f; };
  auto sgn = [](uint16_t v, float scale) { return v == 0x7FFF ? NAN : (int16_t) v / scale; };
  Sen66Values s;
  s.pm1 = pm(w[0]);
  s.pm25 = pm(w[1]);
  s.pm4 = pm(w[2]);
  s.pm10 = pm(w[3]);
  s.humidity = sgn(w[4], 100);
  s.temperature = sgn(w[5], 200);
  s.voc = sgn(w[6], 10);
  s.nox = sgn(w[7], 10);
  s.co2 = w[8] == 0xFFFF ? NAN : (float) w[8];
  return s;
}

struct Sfa40Values {
  float hcho;  // ppb, NAN while warming up or out of spec
  float humidity, temperature;
  bool warming_up, out_of_spec;
};

inline Sfa40Values decode_sfa40(const uint16_t w[4]) {
  Sfa40Values s;
  const uint8_t status = w[3] >> 8;
  s.warming_up = status & 0x01;
  s.out_of_spec = status & 0x02;
  s.hcho = (s.warming_up || s.out_of_spec) ? NAN : w[0] / 10.0f;
  s.humidity = std::fmin(100.0f, std::fmax(0.0f, 125.0f * w[1] / 65535.0f - 6.0f));
  s.temperature = 175.0f * w[2] / 65535.0f - 45.0f;
  return s;
}

}  // namespace air
