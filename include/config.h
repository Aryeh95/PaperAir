#pragma once
// Settings you might want to change. Passwords and keys go in secrets.h instead.

namespace config {

// Time zone as a POSIX TZ string (https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv)
constexpr const char *kTimeZone = "EST5EDT,M3.2.0,M11.1.0";  // US Eastern

constexpr bool kFahrenheit = true;  // false for Celsius
constexpr bool kClock24h = false;

// The SEN66 reads a little warm when it sits next to electronics. Measure against a thermometer
// and put the difference here (degrees C, positive = sensor reads too warm); humidity is
// corrected to match.
constexpr float kTemperatureOffsetC = 0.0f;

// Turn the picture upside down, for mounting the PaperDIY the other way up
constexpr bool kFlipScreen = false;

// The red LED on the back of the board, next to the power switch. It's the power chip's power
// indicator; off by default, since the display is on all the time anyway.
constexpr bool kPowerLed = false;

// How often the screen may redraw when only numbers have changed. Advice changes and button
// presses redraw straight away.
constexpr unsigned long kScreenUpdateMs = 60UL * 1000UL;

// A full black-white-picture refresh this often clears any faint ghosting
constexpr unsigned long kCleanRefreshMs = 60UL * 60UL * 1000UL;

// How often to ask PurpleAir / AirNow for outdoor air. Each PurpleAir request costs a few API
// points; every 10 minutes is about 4,300 requests a month.
constexpr unsigned long kOutdoorUpdateMs = 10UL * 60UL * 1000UL;

// Return from the trends page to the main page after this long
constexpr unsigned long kTrendsTimeoutMs = 2UL * 60UL * 1000UL;

}  // namespace config
