#pragma once
// Outdoor air over Wi-Fi: PurpleAir first (the main sensor, then the backup), AirNow if neither
// has a recent reading.
#include "advice.h"

// Asks the services in turn and returns the first good answer (ok = false if none answered)
air::Outdoor fetch_outdoor();
