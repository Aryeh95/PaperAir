#include "sensors.h"

namespace {
constexpr uint32_t kFreq = 100000;

// SEN66 commands (Sensirion SEN6x datasheet)
constexpr uint16_t kSen66Start = 0x0021, kSen66Stop = 0x0104, kSen66DataReady = 0x0202,
                   kSen66ReadValues = 0x0300;
// SFA40 commands (Sensirion SFA40 datasheet)
constexpr uint16_t kSfa40Start = 0x00AC, kSfa40Stop = 0x50D2, kSfa40ReadValues = 0xC0EB, kSfa40ReadId = 0x02CE;
}  // namespace

bool SensirionDevice::command(uint16_t cmd, const uint16_t *args, size_t n) {
  uint8_t buf[2 + 3 * 4];
  if (n > 4) return false;
  buf[0] = cmd >> 8;
  buf[1] = cmd & 0xFF;
  if (n) air::sensirion_pack(args, n, buf + 2);
  if (!bus_.start(address_, false, kFreq)) {
    bus_.stop();
    return false;
  }
  const bool ok = bus_.write(buf, 2 + 3 * n);
  return bus_.stop() && ok;
}

bool SensirionDevice::read(uint16_t *out, size_t words) {
  uint8_t buf[3 * 9];
  if (words > 9) return false;
  if (!bus_.start(address_, true, kFreq)) {
    bus_.stop();
    return false;
  }
  const bool ok = bus_.read(buf, 3 * words, true);
  bus_.stop();
  return ok && air::sensirion_unpack(buf, words, out);
}

bool SensirionDevice::query(uint16_t cmd, uint32_t wait_ms, uint16_t *out, size_t words) {
  if (!command(cmd)) return false;
  delay(wait_ms);
  return read(out, words);
}

// ---- SEN66 ----------------------------------------------------------------------
bool Sen66::begin() {
  // Stop first, in case it was already measuring before the ESP32 restarted (when it's idle it
  // refuses the command, which is fine)
  if (dev_.command(kSen66Stop)) delay(1000);
  started_ = dev_.command(kSen66Start);
  delay(50);
  return started_;
}

bool Sen66::read(air::Sen66Values &out) {
  uint16_t ready;
  if (!started_ || !dev_.query(kSen66DataReady, 20, &ready, 1) || !(ready & 0xFF)) return false;
  uint16_t w[9];
  if (!dev_.query(kSen66ReadValues, 20, w, 9)) return false;
  out = air::decode_sen66(w);
  return true;
}

// ---- SFA40 ----------------------------------------------------------------------
bool Sfa40::begin() {
  dev_.command(kSfa40Stop);
  delay(25);
  uint16_t id[3];
  if (!dev_.query(kSfa40ReadId, 5, id, 3)) return false;
  found_ = dev_.command(kSfa40Start);
  return found_;
}

bool Sfa40::read(air::Sfa40Values &out) {
  uint16_t w[4];
  if (!found_ || !dev_.query(kSfa40ReadValues, 5, w, 4)) return false;
  out = air::decode_sfa40(w);
  return true;
}
