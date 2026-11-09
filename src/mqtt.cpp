#include "mqtt.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <mqtt_client.h>

#include <atomic>
#include <cmath>

#include "settings.h"
#include "version.h"

namespace {

MqttHooks hooks;
esp_mqtt_client_handle_t client = nullptr;
String uid, base, avail_topic, state_topic, cmd_topic;
String running_key;                      // the broker settings the client was started with
bool running_fahrenheit = true;
std::atomic<bool> connected{false};
std::atomic<bool> need_announce{false};  // send the discovery configs (after connecting, or Home Assistant restarting)
std::atomic<int> pending_cmd{0};         // 1 refresh, 2 page, 3 restart; run in loop(), not in the MQTT task
unsigned long state_ms = 0;
bool announced_hcho = false;

constexpr const char *kDiscovery = "homeassistant";
constexpr unsigned long kStateEveryMs = 30UL * 1000UL;

String broker_key() {
  return settings.mqtt_host + ":" + String(settings.mqtt_port) + "|" + settings.mqtt_user + "|" + settings.mqtt_password;
}

bool enabled() { return settings.mqtt_host.length() > 0; }

void on_event(void *, esp_event_base_t, int32_t id, void *data) {
  auto *e = static_cast<esp_mqtt_event_handle_t>(data);
  switch (id) {
    case MQTT_EVENT_CONNECTED:
      connected = true;
      esp_mqtt_client_subscribe(client, cmd_topic.c_str(), 1);
      esp_mqtt_client_subscribe(client, (String(kDiscovery) + "/status").c_str(), 1);
      need_announce = true;
      Serial.printf("MQTT: connected to %s\n", settings.mqtt_host.c_str());
      break;
    case MQTT_EVENT_DISCONNECTED:
      if (connected) Serial.println("MQTT: disconnected, retrying");
      connected = false;
      break;
    case MQTT_EVENT_DATA: {
      const String topic(e->topic, e->topic_len), msg(e->data, e->data_len);
      if (topic == String(kDiscovery) + "/status") {
        if (msg == "online") need_announce = true;   // Home Assistant restarted: tell it about us again
      } else if (topic == cmd_topic) {
        if (msg == "refresh") pending_cmd = 1;
        else if (msg == "page") pending_cmd = 2;
        else if (msg == "restart") pending_cmd = 3;
      }
      break;
    }
    case MQTT_EVENT_ERROR:
      if (e->error_handle && e->error_handle->connect_return_code)
        Serial.printf("MQTT: broker refused the connection (code %d): check the login\n", (int) e->error_handle->connect_return_code);
      break;
    default:
      break;
  }
}

void stop() {
  if (!client) return;
  if (connected) esp_mqtt_client_publish(client, avail_topic.c_str(), "offline", 0, 1, 1);
  esp_mqtt_client_stop(client);
  esp_mqtt_client_destroy(client);
  client = nullptr;
  connected = false;
}

void start() {
  static String uri, client_id, user, pass;   // the client keeps pointers to these
  uri = "mqtt://" + settings.mqtt_host + ":" + String(settings.mqtt_port);
  client_id = uid;
  user = settings.mqtt_user;
  pass = settings.mqtt_password;
  esp_mqtt_client_config_t cfg = {};
  cfg.broker.address.uri = uri.c_str();
  cfg.credentials.client_id = client_id.c_str();
  if (user.length()) cfg.credentials.username = user.c_str();
  if (pass.length()) cfg.credentials.authentication.password = pass.c_str();
  cfg.session.last_will.topic = avail_topic.c_str();
  cfg.session.last_will.msg = "offline";
  cfg.session.last_will.qos = 1;
  cfg.session.last_will.retain = 1;
  cfg.session.keepalive = 60;
  cfg.network.reconnect_timeout_ms = 10000;
  cfg.buffer.size = 2048;
  cfg.buffer.out_size = 2048;
  client = esp_mqtt_client_init(&cfg);
  if (!client) {
    Serial.println("MQTT: could not create the client");
    return;
  }
  esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, on_event, nullptr);
  esp_mqtt_client_start(client);
  running_key = broker_key();
  running_fahrenheit = settings.fahrenheit;
  Serial.printf("MQTT: connecting to %s\n", uri.c_str());
}

void publish(const String &topic, const String &payload, bool retain) {
  esp_mqtt_client_publish(client, topic.c_str(), payload.c_str(), payload.length(), 1, retain ? 1 : 0);
}

// One discovery config: the device block, availability and state topic are the same for all
void announce_one(const char *component, const char *key, JsonDocument &doc) {
  doc["uniq_id"] = uid + "_" + key;
  doc["avty_t"] = avail_topic;
  JsonObject dev = doc["dev"].to<JsonObject>();
  dev["ids"].to<JsonArray>().add(uid);
  dev["name"] = "PaperAir";
  dev["mf"] = "M5Stack";
  dev["mdl"] = "PaperDIY with SEN66";
  dev["sw"] = kVersion;
  dev["cu"] = "http://" + WiFi.localIP().toString() + "/";
  String out;
  serializeJson(doc, out);
  publish(String(kDiscovery) + "/" + component + "/" + uid + "/" + key + "/config", out, true);
}

void announce_sensor(const char *key, const char *name, const char *unit, const char *device_class, const char *icon = nullptr,
                     const char *category = nullptr, bool numeric = true) {
  JsonDocument doc;
  doc["name"] = name;
  doc["stat_t"] = state_topic;
  doc["val_tpl"] = String("{{ value_json.") + key + " }}";
  if (unit) doc["unit_of_meas"] = unit;
  if (device_class) doc["dev_cla"] = device_class;
  if (numeric) doc["stat_cla"] = "measurement";   // long-term statistics in Home Assistant
  if (icon) doc["ic"] = icon;
  if (category) doc["ent_cat"] = category;
  announce_one("sensor", key, doc);
}

void announce_button(const char *key, const char *name, const char *payload, const char *device_class, const char *icon,
                     const char *category = nullptr) {
  JsonDocument doc;
  doc["name"] = name;
  doc["cmd_t"] = cmd_topic;
  doc["pl_prs"] = payload;
  if (device_class) doc["dev_cla"] = device_class;
  if (icon) doc["ic"] = icon;
  if (category) doc["ent_cat"] = category;
  announce_one("button", key, doc);
}

void remove(const char *component, const char *key) {
  publish(String(kDiscovery) + "/" + component + "/" + uid + "/" + key + "/config", "", true);
}

void announce(const ui::ScreenData &d) {
  announce_sensor("co2", "CO2", "ppm", "carbon_dioxide");
  announce_sensor("pm1", "PM1", "µg/m³", "pm1");
  announce_sensor("pm25", "PM2.5", "µg/m³", "pm25");
  announce_sensor("pm10", "PM10", "µg/m³", "pm10");
  announce_sensor("voc", "VOC index", nullptr, nullptr, "mdi:molecule");
  announce_sensor("nox", "NOx index", nullptr, nullptr, "mdi:smoke");
  if (d.hcho_fitted) announce_sensor("hcho", "Formaldehyde", "ppb", nullptr, "mdi:flask-outline");
  else remove("sensor", "hcho");
  announced_hcho = d.hcho_fitted;
  announce_sensor("temp", "Temperature", d.fahrenheit ? "°F" : "°C", "temperature");
  announce_sensor("humidity", "Humidity", "%", "humidity");
  announce_sensor("indoor_aqi", "Indoor AQI", nullptr, "aqi");
  announce_sensor("outdoor_aqi", "Outdoor AQI", nullptr, "aqi");
  announce_sensor("outdoor_pm25", "Outdoor PM2.5", "µg/m³", "pm25");
  announce_sensor("advice", "Advice", nullptr, nullptr, "mdi:message-text-outline", nullptr, false);
  {
    JsonDocument doc;   // how the air is, as a fixed set of words, for automations
    doc["name"] = "Air status";
    doc["stat_t"] = state_topic;
    doc["val_tpl"] = "{{ value_json.status }}";
    doc["dev_cla"] = "enum";
    JsonArray ops = doc["ops"].to<JsonArray>();
    for (const char *o : {"starting", "no_sensor", "fresh", "fine", "act", "urgent"}) ops.add(o);
    doc["ic"] = "mdi:home-thermometer-outline";
    announce_one("sensor", "status", doc);
  }
  announce_sensor("rssi", "Wi-Fi signal", "dBm", "signal_strength", nullptr, "diagnostic");
  announce_button("refresh", "Refresh screen", "refresh", nullptr, "mdi:refresh");
  announce_button("page", "Switch page", "page", nullptr, "mdi:swap-horizontal");
  announce_button("restart", "Restart", "restart", "restart", nullptr, "config");
  publish(avail_topic, "online", true);
  Serial.println("MQTT: sent the Home Assistant discovery configs");
}

void set_number(JsonDocument &doc, const char *key, float v, int decimals) {
  if (std::isnan(v)) doc[key] = nullptr;
  else doc[key] = std::round(v * std::pow(10.0f, decimals)) / std::pow(10.0f, decimals);
}

void send_state(const ui::ScreenData &d) {
  static const char *kKeys[air::kMetrics] = {"co2", "pm1", "pm25", "pm10", "voc", "nox", "hcho", "temp", "humidity"};
  static const char *kMood[] = {"starting", "no_sensor", "fresh", "fine", "act", "urgent"};
  JsonDocument doc;
  for (int m = 0; m < air::kMetrics; m++) {
    float v = d.now.v[m];
    if (m == (int) air::Metric::Temp && d.fahrenheit && !std::isnan(v)) v = v * 9 / 5 + 32;
    set_number(doc, kKeys[m], v, m == (int) air::Metric::Temp || (m >= 1 && m <= 3) ? 1 : 0);
  }
  if (d.indoor_aqi >= 0) doc["indoor_aqi"] = d.indoor_aqi;
  else doc["indoor_aqi"] = nullptr;
  if (d.outdoor.ok) {
    doc["outdoor_aqi"] = d.outdoor.aqi;
    set_number(doc, "outdoor_pm25", d.outdoor.pm25, 1);
  } else {
    doc["outdoor_aqi"] = nullptr;
    doc["outdoor_pm25"] = nullptr;
  }
  doc["advice"] = d.advice.headline;
  const int mood = (int) d.advice.mood;
  doc["status"] = mood >= 0 && mood < 6 ? kMood[mood] : "starting";
  doc["rssi"] = WiFi.RSSI();
  String out;
  serializeJson(doc, out);
  publish(state_topic, out, false);
}

}  // namespace

void mqtt_begin(const MqttHooks &h) {
  hooks = h;
  char id[16];
  snprintf(id, sizeof id, "%06llx", (unsigned long long) (ESP.getEfuseMac() >> 24) & 0xffffffULL);
  uid = String("paperair_") + id;
  base = String("paperair/") + id;
  avail_topic = base + "/status";
  state_topic = base + "/state";
  cmd_topic = base + "/cmd";
}

void mqtt_reconfigure() {
  if (client && (broker_key() != running_key || !enabled())) stop();   // started again by mqtt_loop()
  else if (client && settings.fahrenheit != running_fahrenheit) {
    running_fahrenheit = settings.fahrenheit;
    need_announce = true;   // the temperature's unit changed
  }
}

void mqtt_loop() {
  if (!enabled()) return;
  if (!client) {
    if (WiFi.status() == WL_CONNECTED) start();
    return;
  }
  switch (pending_cmd.exchange(0)) {
    case 1: hooks.refresh(); break;
    case 2: hooks.toggle_page(); break;
    case 3:
      Serial.println("MQTT: restart requested");
      publish(avail_topic, "offline", true);
      delay(300);
      ESP.restart();
      break;
    default: break;
  }
  if (!connected) return;
  const bool announce_now = need_announce.exchange(false);
  if (!announce_now && millis() - state_ms < kStateEveryMs) return;
  const ui::ScreenData d = hooks.data();
  if (announce_now || d.hcho_fitted != announced_hcho) announce(d);
  send_state(d);
  state_ms = millis();
}

String mqtt_status() {
  if (!enabled()) return "off";
  if (connected) return "connected to " + settings.mqtt_host;
  return "connecting to " + settings.mqtt_host;
}
