#pragma once
// A small web page served by the PaperDIY itself, on the local network: the current readings and
// advice, the 24-hour trends, outdoor air, and the device's state, behind a password from
// the settings page. Also a JSON API for other things on the network (Home Assistant and the like) and a
// CSV of the history.
//
// In setup mode (the PaperAir-Setup hotspot) the settings page needs no login and every address
// leads to it, so a phone that joins the hotspot opens it by itself.
//
//   http://paperair.local/        the page (or the IP address from the serial log)
//   GET  /api/now                 readings, advice, outdoor, device state
//   GET  /api/history             the 24-hour history, oldest first
//   GET  /history.csv             the same as a spreadsheet
//   POST /api/refresh             redraw the screen with a full refresh
//   POST /api/page                switch the screen between the main and trends pages
//   POST /api/restart             reboot
//   GET  /settings                the settings page: Wi-Fi, outdoor-air keys, password, units
//   POST /api/setup               open the setup hotspot
//   GET  /api/scan                Wi-Fi networks in range
//
// The API needs the same login as the page; send the password as a "Bearer" token in the
// Authorization header, or log in and use the cookie.
#include <functional>

#include "screen.h"

struct WebHooks {
  std::function<ui::ScreenData()> data;   // what the screen shows now
  std::function<void()> refresh;          // full refresh of the screen
  std::function<void()> toggle_page;
  std::function<bool()> sen66_started;
  std::function<unsigned long()> sensor_age_ms;   // since the last good SEN66 reading
  std::function<unsigned long()> outdoor_age_ms;  // since the last good outdoor reading
  std::function<bool()> in_setup;                 // the setup hotspot is up
  std::function<void(bool wifi_changed)> settings_changed;   // after the settings page saves
  std::function<void()> enter_setup;              // open the setup hotspot
};

void web_begin(const WebHooks &hooks);
void web_loop();
