#include "settings.h"

#include <Preferences.h>

#include "config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#endif
#ifndef PURPLEAIR_API_KEY
#define PURPLEAIR_API_KEY ""
#define PURPLEAIR_SENSOR_INDEX 0
#define PURPLEAIR_BACKUP_SENSOR_INDEX 0
#endif
#ifndef AIRNOW_API_KEY
#define AIRNOW_API_KEY ""
#define AIRNOW_ZIP_CODE ""
#endif
#ifndef WEB_PASSWORD
#define WEB_PASSWORD ""
#endif

Settings settings;

namespace {
constexpr const char *kNamespace = "paperair-cfg";
}

void settings_load() {
  Preferences p;
  p.begin(kNamespace, true);
  settings.ssid = p.getString("ssid", WIFI_SSID);
  settings.wifi_password = p.getString("pass", WIFI_PASSWORD);
  settings.static_ip = p.getString("ip", "");
  settings.subnet = p.getString("mask", "255.255.255.0");
  settings.gateway = p.getString("gw", "");
  settings.dns = p.getString("dns", "");
  settings.roam = p.getBool("roam", true);
  settings.purpleair_key = p.getString("pa_key", PURPLEAIR_API_KEY);
  settings.purpleair_index = p.getInt("pa_idx", PURPLEAIR_SENSOR_INDEX);
  settings.purpleair_backup = p.getInt("pa_bak", PURPLEAIR_BACKUP_SENSOR_INDEX);
  settings.airnow_key = p.getString("an_key", AIRNOW_API_KEY);
  settings.airnow_zip = p.getString("an_zip", AIRNOW_ZIP_CODE);
  settings.web_password = p.getString("web_pw", WEB_PASSWORD);
  settings.temperature_offset_c = p.getFloat("t_off", config::kTemperatureOffsetC);
  settings.fahrenheit = p.getBool("fahr", config::kFahrenheit);
  settings.clock_24h = p.getBool("clk24", config::kClock24h);
  settings.time_zone = p.getString("tz", config::kTimeZone);
  p.end();
}

void settings_save() {
  Preferences p;
  p.begin(kNamespace, false);
  p.putString("ssid", settings.ssid);
  p.putString("pass", settings.wifi_password);
  p.putString("ip", settings.static_ip);
  p.putString("mask", settings.subnet);
  p.putString("gw", settings.gateway);
  p.putString("dns", settings.dns);
  p.putBool("roam", settings.roam);
  p.putString("pa_key", settings.purpleair_key);
  p.putInt("pa_idx", settings.purpleair_index);
  p.putInt("pa_bak", settings.purpleair_backup);
  p.putString("an_key", settings.airnow_key);
  p.putString("an_zip", settings.airnow_zip);
  p.putString("web_pw", settings.web_password);
  p.putFloat("t_off", settings.temperature_offset_c);
  p.putBool("fahr", settings.fahrenheit);
  p.putBool("clk24", settings.clock_24h);
  p.putString("tz", settings.time_zone);
  p.end();
}
