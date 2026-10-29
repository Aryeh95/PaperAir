#include "web.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>

#include <cmath>
#include <cstring>

#include "advice.h"
#include "aqi.h"
#include "config.h"
#include "settings.h"
#include "web_page.h"

namespace {

constexpr const char *kVersion = "1.2";
constexpr const char *kMetricKeys[air::kMetrics] = {"co2", "pm1", "pm25", "pm10", "voc", "nox", "hcho", "temp", "humidity"};

WebServer server(80);
WebHooks hooks;
String session;  // the cookie value of the one signed-in session; empty = nobody signed in

bool password_set() { return settings.web_password.length() > 0; }

bool signed_in() {
  if (!password_set()) return true;
  if (server.hasHeader("Cookie")) {
    const String c = server.header("Cookie");
    const int i = c.indexOf("sid=");
    if (i >= 0 && session.length()) {
      String v = c.substring(i + 4);
      const int e = v.indexOf(';');
      if (e >= 0) v = v.substring(0, e);
      if (v == session) return true;
    }
  }
  if (server.hasHeader("Authorization")) {
    const String a = server.header("Authorization");
    if (a.startsWith("Bearer ") && a.substring(7) == settings.web_password) return true;
  }
  return false;
}

bool require_login(bool page) {
  if (signed_in()) return true;
  if (page) {
    server.sendHeader("Location", "/login");
    server.send(302, "text/plain", "");
  } else {
    server.send(401, "application/json", "{\"error\":\"sign in\"}");
  }
  return false;
}

// On the setup hotspot the settings page is open (the hotspot has its own password)
bool in_setup() { return hooks.in_setup && hooks.in_setup(); }

bool on_hotspot() {
  return in_setup() && server.hostHeader() != WiFi.localIP().toString() && !server.hostHeader().startsWith("paperair.local");
}

bool settings_allowed() { return on_hotspot() || require_login(true); }

String ago(unsigned long ms) {
  const unsigned long s = ms / 1000;
  char buf[32];
  if (s < 60) snprintf(buf, sizeof buf, "%lu s ago", s);
  else if (s < 3600) snprintf(buf, sizeof buf, "%lu min ago", s / 60);
  else snprintf(buf, sizeof buf, "%lu h %lu min ago", s / 3600, (s % 3600) / 60);
  return buf;
}

String uptime() {
  const unsigned long s = millis() / 1000;
  char buf[40];
  if (s < 3600) snprintf(buf, sizeof buf, "%lu min", s / 60);
  else if (s < 86400) snprintf(buf, sizeof buf, "%lu h %lu min", s / 3600, (s % 3600) / 60);
  else snprintf(buf, sizeof buf, "%lu d %lu h", s / 86400, (s % 86400) / 3600);
  return buf;
}

String html_escape(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (const char c : s) {
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      case '\'': o += "&#39;"; break;
      default: o += c;
    }
  }
  return o;
}

void set_number(JsonObject o, const char *key, float v, int decimals) {
  if (std::isnan(v)) o[key] = nullptr;
  else o[key] = std::round(v * std::pow(10.0f, decimals)) / std::pow(10.0f, decimals);
}

int decimals_for(int m) { return m == (int) air::Metric::Temp || (m >= 1 && m <= 3) ? 1 : 0; }

void handle_now() {
  if (!require_login(false)) return;
  const ui::ScreenData d = hooks.data();
  JsonDocument doc;
  JsonObject r = doc["readings"].to<JsonObject>();
  for (int m = 0; m < air::kMetrics; m++) {
    float v = d.now.v[m];
    if (m == (int) air::Metric::Temp && d.fahrenheit && !std::isnan(v)) v = v * 9 / 5 + 32;
    set_number(r, kMetricKeys[m], v, decimals_for(m));
  }
  doc["fahrenheit"] = d.fahrenheit;
  doc["hcho_fitted"] = d.hcho_fitted;
  doc["indoor_aqi"] = d.indoor_aqi;
  JsonObject a = doc["advice"].to<JsonObject>();
  a["headline"] = d.advice.headline;
  a["detail"] = d.advice.detail;
  a["outdoor"] = d.advice.outdoor;
  a["mood"] = (int) d.advice.mood;
  JsonObject o = doc["outdoor"].to<JsonObject>();
  o["ok"] = d.outdoor.ok;
  if (d.outdoor.ok) {
    o["aqi"] = d.outdoor.aqi;
    o["category"] = air::aqi_word(d.outdoor.aqi);
    set_number(o, "pm25", d.outdoor.pm25, 1);
    o["age"] = ago(hooks.outdoor_age_ms());
    o["source"] = settings.purpleair_key.length() ? "PurpleAir" : "AirNow";
  }
  JsonObject lim = doc["limits"].to<JsonObject>();
  using namespace air::limits;
  lim["co2"]["watch"] = kCo2Watch;
  lim["co2"]["act"] = kCo2Act;
  lim["pm25"]["watch"] = kPm25Watch;
  lim["pm25"]["act"] = kPm25Act;
  lim["hcho"]["watch"] = kHchoWatch;
  lim["hcho"]["act"] = kHchoAct;
  lim["voc"]["watch"] = kVocWatch;
  lim["voc"]["act"] = kVocAct;
  lim["nox"]["watch"] = kNoxWatch;
  lim["nox"]["act"] = kNoxAct;
  lim["humidity"]["low"] = kHumidityLow;
  lim["humidity"]["high"] = kHumidityHigh;
  doc["time_valid"] = d.time_valid;
  if (d.time_valid) {
    char buf[64];
    strftime(buf, sizeof buf, d.clock_24h ? "%A %e %B · %H:%M" : "%A %e %B · %l:%M %p", &d.local_time);
    doc["time"] = buf;
  } else {
    doc["time"] = "Clock not set yet";
  }
  doc["history_version"] = d.history ? d.history->version() : 0;
  doc["setup_mode"] = in_setup();
  JsonObject dev = doc["device"].to<JsonObject>();
  dev["ssid"] = WiFi.SSID();
  dev["rssi"] = WiFi.RSSI();
  dev["ip"] = WiFi.localIP().toString();
  dev["uptime"] = uptime();
  dev["heap"] = String(ESP.getFreeHeap() / 1024) + " KB";
  dev["version"] = kVersion;
  dev["sen66"] = hooks.sen66_started() ? "reading, " + ago(hooks.sensor_age_ms()) : "not found";
  dev["sfa40"] = d.hcho_fitted ? "found" : "not fitted";
  dev["page"] = d.page == ui::Page::Main ? "main" : "trends";
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

// The history is 288 points x 9 metrics; sent in pieces rather than built as one string
void handle_history() {
  if (!require_login(false)) return;
  const ui::ScreenData d = hooks.data();
  const air::History *h = d.history;
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", "");
  String s;
  s.reserve(4096);
  s += "{\"interval_s\":300,\"slots\":" + String(air::History::kSlots) + ",\"version\":" + String(h ? h->version() : 0) + ",\"metrics\":{";
  const int n = h ? std::min(h->size(), air::History::kSlots) : 0;
  for (int m = 0; m < air::kMetrics; m++) {
    s += (m ? ",\"" : "\"") + String(kMetricKeys[m]) + "\":[";
    for (int age = n - 1; age >= 0; age--) {  // oldest first
      float v = h->at((air::Metric) m, age);
      if (m == (int) air::Metric::Temp && d.fahrenheit && !std::isnan(v)) v = v * 9 / 5 + 32;
      if (std::isnan(v)) s += "null";
      else s += String(v, decimals_for(m));
      if (age) s += ',';
      if (s.length() > 3500) {
        server.sendContent(s);
        s = "";
      }
    }
    s += ']';
  }
  s += "}}";
  server.sendContent(s);
  server.sendContent("");
}

void handle_csv() {
  if (!require_login(true)) return;
  const ui::ScreenData d = hooks.data();
  const air::History *h = d.history;
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Disposition", "attachment; filename=paperair-history.csv");
  server.send(200, "text/csv", "");
  String s = "minutes_ago,co2_ppm,pm1,pm25,pm10,voc_index,nox_index,hcho_ppb,temp_";
  s += d.fahrenheit ? "f" : "c";
  s += ",humidity_pct\n";
  const int n = h ? std::min(h->size(), air::History::kSlots) : 0;
  for (int age = n - 1; age >= 0; age--) {
    s += String(age * 5);
    for (int m = 0; m < air::kMetrics; m++) {
      float v = h->at((air::Metric) m, age);
      if (m == (int) air::Metric::Temp && d.fahrenheit && !std::isnan(v)) v = v * 9 / 5 + 32;
      s += ',';
      if (!std::isnan(v)) s += String(v, 1);
    }
    s += '\n';
    if (s.length() > 3500) {
      server.sendContent(s);
      s = "";
    }
  }
  server.sendContent(s);
  server.sendContent("");
}

void handle_login_page(const char *err = "") {
  String page = FPSTR(kLoginPage);
  page.replace("%ERR%", err);
  server.send(200, "text/html", page);
}

void handle_login_post() {
  if (!password_set() || server.arg("password") == settings.web_password) {
    char buf[33];
    snprintf(buf, sizeof buf, "%08lx%08lx%08lx%08lx", (unsigned long) esp_random(), (unsigned long) esp_random(),
             (unsigned long) esp_random(), (unsigned long) esp_random());
    session = buf;
    server.sendHeader("Set-Cookie", "sid=" + session + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=2592000");
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  } else {
    delay(500);  // slow down guessing
    handle_login_page("<p class=\"err\">Wrong password.</p>");
  }
}

void handle_settings_page() {
  if (!settings_allowed()) return;
  String page = FPSTR(kSettingsPage);
  const bool hotspot = on_hotspot();
  page.replace("%BACK%", hotspot ? "/settings" : "/");
  page.replace("%SETUP%", hotspot ? "<div class=\"card notice\"><b>Setup mode.</b> Enter your Wi-Fi below and save. The PaperDIY joins the "
                                    "network, the screen shows when it's connected, and from then on the page is at "
                                    "<b>http://paperair.local/</b> on that network.</div>"
                                  : "");
  page.replace("%SSID%", html_escape(settings.ssid));
  page.replace("%PASS_PH%", settings.wifi_password.length() ? "(unchanged)" : "");
  page.replace("%PA_KEY%", html_escape(settings.purpleair_key));
  page.replace("%PA_IDX%", String(settings.purpleair_index));
  page.replace("%PA_BAK%", String(settings.purpleair_backup));
  page.replace("%AN_KEY%", html_escape(settings.airnow_key));
  page.replace("%AN_ZIP%", html_escape(settings.airnow_zip));
  page.replace("%F_SEL%", settings.fahrenheit ? "checked" : "");
  page.replace("%C_SEL%", settings.fahrenheit ? "" : "checked");
  page.replace("%H12_SEL%", settings.clock_24h ? "" : "checked");
  page.replace("%H24_SEL%", settings.clock_24h ? "checked" : "");
  page.replace("%T_OFF%", String(settings.temperature_offset_c, 1));
  page.replace("%TZ%", html_escape(settings.time_zone));
  page.replace("%WEB_PH%", password_set() ? "(unchanged)" : "none set, the page is open to the network");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", page);
}

void handle_settings_post() {
  if (!settings_allowed()) return;
  bool wifi_changed = false;
  String ssid = server.arg("ssid");
  ssid.trim();
  if (ssid != settings.ssid) {
    settings.ssid = ssid;
    wifi_changed = true;
  }
  if (server.arg("pass").length()) {
    settings.wifi_password = server.arg("pass");
    wifi_changed = true;
  }
  settings.purpleair_key = server.arg("pa_key");
  settings.purpleair_key.trim();
  settings.purpleair_index = server.arg("pa_idx").toInt();
  settings.purpleair_backup = server.arg("pa_bak").toInt();
  settings.airnow_key = server.arg("an_key");
  settings.airnow_key.trim();
  settings.airnow_zip = server.arg("an_zip");
  settings.airnow_zip.trim();
  settings.fahrenheit = server.arg("units") != "c";
  settings.clock_24h = server.arg("clock") == "24";
  settings.temperature_offset_c = server.arg("t_off").toFloat();
  String tz = server.arg("tz");
  tz.trim();
  if (tz.length()) settings.time_zone = tz;
  const String pw = server.arg("web_pw");
  if (pw == " ") {
    settings.web_password = "";
  } else if (pw.length()) {
    settings.web_password = pw;
    session = "";  // sign everyone out
  }
  if (hooks.settings_changed) hooks.settings_changed(wifi_changed && settings.have_wifi());

  String page = FPSTR(kSavedPage);
  const bool hotspot = on_hotspot();
  if (wifi_changed && settings.have_wifi()) {
    page.replace("%MSG%", "<p>Joining <b>" + html_escape(settings.ssid) + "</b>. Watch the screen: the Wi-Fi mark appears when it's connected, "
                          "and then the page is at <b>http://paperair.local/</b> on that network.</p>" +
                          (hotspot ? "<p class=\"muted\">The setup hotspot stays open for a minute in case the password was wrong.</p>" : ""));
    page.replace("%REFRESH%", "");
    page.replace("%NEXT%", hotspot ? "/settings" : "/");
  } else {
    page.replace("%MSG%", "<p>Settings saved.</p>");
    page.replace("%REFRESH%", hotspot ? "" : "<meta http-equiv=\"refresh\" content=\"2;url=/\">");
    page.replace("%NEXT%", hotspot ? "/settings" : "/");
  }
  server.send(200, "text/html", page);
}

void handle_scan() {
  if (!settings_allowed()) return;
  if (!(WiFi.getMode() & WIFI_MODE_STA)) WiFi.mode(WIFI_AP_STA);
  const int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    if (!WiFi.SSID(i).length()) continue;
    bool dup = false;
    for (JsonObject o : arr)
      if (o["ssid"] == WiFi.SSID(i)) {   // keep the strongest of each name
        dup = true;
        if (WiFi.RSSI(i) > o["rssi"].as<int>()) o["rssi"] = WiFi.RSSI(i);
      }
    if (dup) continue;
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

}  // namespace

void web_begin(const WebHooks &h) {
  hooks = h;
  const char *headers[] = {"Cookie", "Authorization", "Host"};
  server.collectHeaders(headers, 3);
  server.on("/", HTTP_GET, [] {
    if (on_hotspot()) {   // a phone on the setup hotspot wants the settings page
      server.sendHeader("Location", "/settings");
      server.send(302, "text/plain", "");
      return;
    }
    if (!require_login(true)) return;
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/html", FPSTR(kPage));
  });
  server.on("/login", HTTP_GET, [] { handle_login_page(); });
  server.on("/login", HTTP_POST, handle_login_post);
  server.on("/logout", HTTP_GET, [] {
    session = "";
    server.sendHeader("Set-Cookie", "sid=; Path=/; Max-Age=0");
    server.sendHeader("Location", "/login");
    server.send(302, "text/plain", "");
  });
  server.on("/settings", HTTP_GET, handle_settings_page);
  server.on("/settings", HTTP_POST, handle_settings_post);
  server.on("/api/scan", HTTP_GET, handle_scan);
  server.on("/api/now", HTTP_GET, handle_now);
  server.on("/api/history", HTTP_GET, handle_history);
  server.on("/history.csv", HTTP_GET, handle_csv);
  server.on("/api/refresh", HTTP_POST, [] {
    if (!require_login(false)) return;
    hooks.refresh();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/page", HTTP_POST, [] {
    if (!require_login(false)) return;
    hooks.toggle_page();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/setup", HTTP_POST, [] {
    if (!require_login(false)) return;
    if (hooks.enter_setup) hooks.enter_setup();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/restart", HTTP_POST, [] {
    if (!require_login(false)) return;
    server.send(200, "application/json", "{\"ok\":true}");
    delay(200);
    ESP.restart();
  });
  server.onNotFound([] {
    if (in_setup()) {   // captive portal: every address a phone probes leads to the settings page
      server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/settings");
      server.send(302, "text/plain", "");
      return;
    }
    server.send(404, "text/plain", "Not found");
  });
  server.begin();
  if (!password_set()) Serial.println("Web: no password set, the page is open to the network (set one on /settings)");
}

void web_loop() {
  static bool mdns = false;
  if (!mdns && WiFi.status() == WL_CONNECTED) {
    mdns = MDNS.begin("paperair");
    if (mdns) {
      MDNS.addService("http", "tcp", 80);
      Serial.println("Web: http://paperair.local/");
    }
  }
  server.handleClient();
}
