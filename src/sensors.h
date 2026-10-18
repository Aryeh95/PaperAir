#pragma once
// The SEN66 (particles, CO2, VOC, NOx, temperature, humidity) and the optional SFA40
// (formaldehyde), both on the PaperDIY's I2C bus (G41 SDA, G42 SCL) with its power chip.
// They're driven through M5Unified's I2C so everything on the bus goes through one driver.
#include <M5Unified.h>

#include "sensirion.h"

class SensirionDevice {
 public:
  SensirionDevice(m5::I2C_Class &bus, uint8_t address) : bus_(bus), address_(address) {}

  // Sends a 16-bit command, then any argument words with their checksums
  bool command(uint16_t cmd, const uint16_t *args = nullptr, size_t n = 0);
  // Reads `words` 16-bit values; false if the device doesn't answer or a checksum is wrong
  bool read(uint16_t *out, size_t words);
  // command, wait, read
  bool query(uint16_t cmd, uint32_t wait_ms, uint16_t *out, size_t words);

 private:
  m5::I2C_Class &bus_;
  uint8_t address_;
};

class Sen66 {
 public:
  explicit Sen66(m5::I2C_Class &bus) : dev_(bus, 0x6B) {}
  bool begin();                       // false if there's no SEN66
  bool read(air::Sen66Values &out);   // true when a new reading came in (once a second)
  bool started() const { return started_; }

 private:
  SensirionDevice dev_;
  bool started_ = false;
};

class Sfa40 {
 public:
  explicit Sfa40(m5::I2C_Class &bus) : dev_(bus, 0x5D) {}
  bool begin();                       // false if there's no SFA40
  bool read(air::Sfa40Values &out);
  bool found() const { return found_; }

 private:
  SensirionDevice dev_;
  bool found_ = false;
};
