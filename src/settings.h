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
  bool voc_alerts = true;                   // false: the VOC index never triggers advice
  bool co2_alerts = true;                   // false: CO2 never triggers advice
  // The main page's plots, as air::Metric numbers. plot_big -1: automatic (what the advice is about)
  int plot_big = -1;
  int plot_side[3] = {2, 4, 5};             // PM2.5, VOC, NOx
  int plot_hours = 24;                      // 6, 12 or 24
  bool plot_fit = false;                    // scale the plots to their data
  String time_zone;
  String mqtt_host, mqtt_user, mqtt_password;   // Home Assistant's MQTT broker; empty host = off
  int mqtt_port = 1883;

  bool have_wifi() const { return ssid.length() > 0; }
  bool have_static_ip() const { return static_ip.length() > 0; }
};

extern Settings settings;

void settings_load();  // from flash, with the compiled-in defaults for anything not set
void settings_save();
