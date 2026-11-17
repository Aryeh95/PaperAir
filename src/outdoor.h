#pragma once
// Outdoor air over Wi-Fi: PurpleAir first (the main sensor, then the backup), AirNow if neither
// has a recent reading. Fetched in a background task so slow or unreachable servers never stall
// buttons, sensor reads or the web page.
#include "advice.h"

void outdoor_start();                  // starts a background fetch if none is running
bool outdoor_busy();
bool outdoor_poll(air::Outdoor &out);  // true once when a fetch finishes
