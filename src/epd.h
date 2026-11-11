#pragma once
// The e-paper panel, driven by EPDiy instead of M5GFX (test branch).
//
// M5GFX's quality mode re-runs a flashing waveform on every pixel that changes, so the clock and
// the readings blink each minute. EPDiy keeps the frame on the glass and the next one, and drives
// only the pixels that differ, with GL16: a grey-to-grey waveform that doesn't flash. Once an hour
// (and on Key B) the screen is cleaned: one flash to white, then the picture drawn from white.
#include <cstdint>

namespace epd {

enum class Update {
  Gentle,   // only the changed pixels, without flashing
  Clean,    // flash to white, then draw everything: clears ghosting
};

// Takes the panel over from M5GFX. Call after M5.begin() and the screen rotation are set.
bool begin();

// Shows a 960x540 8-bit grey frame (0 black, 255 white).
void show(const uint8_t *grey, Update how);

}  // namespace epd
