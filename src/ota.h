#pragma once
// Firmware updates over Wi-Fi, two ways, and the safety net under both:
//
//   pio run -e paperdiy-ota -t upload     from PlatformIO (ArduinoOTA, port 3232, the web password)
//   the Firmware card on /settings        upload a firmware.bin from a browser
//
// A freshly installed firmware boots "pending": if it crashes or is restarted before it has run
// for a while and answered a web request, the bootloader goes back to the previous one. Once it
// has, it is confirmed and the previous firmware stays in the other slot, where "Boot the
// previous firmware" on the settings page can still reach it.
#include <Arduino.h>

void ota_begin(const String &password);   // once Wi-Fi and mDNS are up
void ota_loop();
void ota_web_served();                     // the web page answered a request: the firmware works
bool ota_pending();                        // this firmware is not yet confirmed
bool ota_can_roll_back();                  // the other slot holds a firmware
void ota_roll_back();                      // boot it (restarts)
