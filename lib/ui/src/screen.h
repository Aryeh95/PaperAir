#pragma once
// The two screens, drawn into a 960x540 greyscale canvas:
//   main    one sentence saying what to do, the reading behind it with its last 24 hours,
//           and every other reading along the bottom
//   trends  24-hour charts (Key B)
// The same code runs on the PaperDIY and in the desktop preview.
#include <M5GFX.h>

#include <ctime>
#include <string>

#include "advice.h"
#include "readings.h"

namespace ui {

constexpr int kWidth = 960;
constexpr int kHeight = 540;

enum class Page { Main, Trends };

struct ScreenData {
  air::Readings now;                    // latest indoor readings (NAN = none)
  const air::History *history = nullptr;
  air::Advice advice;
  air::Outdoor outdoor;
  int indoor_aqi = -1;                  // from PM2.5
  bool hcho_fitted = false;             // an SFA40 is connected
  bool fahrenheit = true;
  bool clock_24h = false;
  bool wifi_ok = true;
  bool time_valid = false;
  std::tm local_time{};
  Page page = Page::Main;
};

void draw(M5Canvas &canvas, const ScreenData &d);

// Text that changes whenever anything visible changes, so the screen only refreshes when needed.
std::string signature(const ScreenData &d);

}  // namespace ui
