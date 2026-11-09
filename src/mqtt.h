#pragma once
// Home Assistant over MQTT, with discovery: PaperAir shows up in Home Assistant by itself, as one
// device with its readings, the advice, the outdoor air, and buttons to refresh the screen, switch
// its page and restart it. Needs a broker (the Mosquitto add-on) and the MQTT integration; the
// broker's address and a login go on the settings page. Leave the address empty to turn it off.
//
//   paperair/<id>/state     the readings as JSON, every 30 seconds
//   paperair/<id>/status    "online" / "offline" (the last will), retained
//   paperair/<id>/cmd       "refresh", "page" or "restart"
//   homeassistant/...       the discovery configs, retained; sent again when Home Assistant restarts
#include <Arduino.h>

#include <functional>

#include "screen.h"

struct MqttHooks {
  std::function<ui::ScreenData()> data;   // what the screen shows now
  std::function<void()> refresh;          // full refresh of the screen
  std::function<void()> toggle_page;
};

void mqtt_begin(const MqttHooks &hooks);
void mqtt_loop();
void mqtt_reconfigure();   // after the settings page saves: reconnects if the broker or units changed
String mqtt_status();      // "off", "connecting to ...", "connected to ..."
