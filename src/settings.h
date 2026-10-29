#pragma once
// Settings that can be changed without reflashing: Wi-Fi, the outdoor-air keys, the web password,
// the temperature offset and the units. Kept in flash (NVS). The values in secrets.h and config.h
// are the defaults the first time, so a build from a filled-in secrets.h works as before.
#include <Arduino.h>

struct Settings {
  String ssid, wifi_password;
  String static_ip, subnet, gateway, dns;   // empty static_ip = DHCP
  bool roam = true;                         // mesh: join the strongest access point, move when weak
  String purpleair_key;
  int purpleair_index = 0, purpleair_backup = 0;
  String airnow_key, airnow_zip;
  String web_password;
  float temperature_offset_c = 0;
  bool fahrenheit = true;
  bool clock_24h = false;
  String time_zone;

  bool have_wifi() const { return ssid.length() > 0; }
  bool have_static_ip() const { return static_ip.length() > 0; }
};

extern Settings settings;

void settings_load();  // from flash, with the compiled-in defaults for anything not set
void settings_save();
