// PaperAir: an air quality display for the M5Stack PaperDIY.
//
// Reads the SEN66 (and an SFA40 if there is one) every second, fetches outdoor air every 10
// minutes, and redraws the e-paper screen when something worth showing has changed.
// Buttons: G4 switches between the main page and the 24-hour trends; G3 does a full refresh.
#include <DNSServer.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>

#include <ctime>

#include "advice.h"
#include "aqi.h"
#include "config.h"
#include "humidity.h"
#include "outdoor.h"
#include "readings.h"
#include "screen.h"
#include "sensors.h"
#include "settings.h"
#include "web.h"

using air::Metric;

namespace {

M5Canvas canvas(&M5.Display);
Sen66 sen66(m5::In_I2C);  // not M5.In_I2C: that reference is only set once M5 is constructed,
                           // which may be after these globals
Sfa40 sfa40(m5::In_I2C);

air::History history;
Preferences store;             // the history, saved to flash every 5 minutes
bool history_restored = false;
unsigned long history_saved_ms = 0;
air::Readings latest;
bool sensor_seen = false;
unsigned long sensor_ms = 0;      // last good SEN66 reading
unsigned long sensor_try_ms = 0;  // last attempt to start the SEN66
float hcho = NAN;
unsigned long hcho_ms = 0;

air::Outdoor outdoor;
unsigned long outdoor_ms = 0;      // last good outdoor reading
unsigned long outdoor_try_ms = 0;  // last attempt
bool outdoor_tried = false;

ui::Page page = ui::Page::Main;
unsigned long page_ms = 0;
std::string shown_signature, shown_headline;
unsigned long drawn_ms = 0, cleaned_ms = 0;
bool redraw_now = true, clean_now = true;

// Setup mode: a hotspot and a captive portal for the settings page, when there's no Wi-Fi to join,
// when joining keeps failing, or when G3 is held for 3 seconds
bool setup_mode = false;
unsigned long setup_since = 0;
unsigned long wifi_begin_ms = 0;
DNSServer dns;
constexpr const char *kSetupSsid = "PaperAir-Setup", *kSetupPassword = "paperair";
constexpr unsigned long kSetupTimeoutMs = 15UL * 60UL * 1000UL;   // then back to trying the Wi-Fi

void enter_setup() {
  if (setup_mode) return;
  setup_mode = true;
  setup_since = millis();
  WiFi.mode(settings.have_wifi() ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(kSetupSsid, kSetupPassword);
  dns.start(53, "*", WiFi.softAPIP());   // every name resolves to us, so phones open the portal
  Serial.printf("Setup mode: join %s (password %s), then open http://%s/\n", kSetupSsid, kSetupPassword,
                WiFi.softAPIP().toString().c_str());
  redraw_now = true;
}

void leave_setup() {
  if (!setup_mode) return;
  setup_mode = false;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  if (settings.have_wifi()) {
    WiFi.begin(settings.ssid.c_str(), settings.wifi_password.c_str());
    wifi_begin_ms = millis();
  }
  redraw_now = true;
}

bool elapsed(unsigned long since, unsigned long ms) { return millis() - since >= ms; }

void setup_display() {
  // Landscape, the right way up for the PaperDIY standing on its long edge
  int rotation = M5.Display.getRotation();
  if (M5.Display.width() < M5.Display.height()) rotation = (rotation + 1) & 3;
  if (config::kFlipScreen) rotation = (rotation + 2) & 3;
  M5.Display.setRotation(rotation);
  M5.Display.setEpdMode(epd_mode_t::epd_quality);

  canvas.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  canvas.setPsram(true);
  if (!canvas.createSprite(ui::kWidth, ui::kHeight)) Serial.println("Not enough memory for the screen");
}

void read_sensors() {
  static unsigned long read_ms = 0;
  if (!elapsed(read_ms, 1000)) return;
  read_ms = millis();

  if (!sen66.started() && (sensor_try_ms == 0 || elapsed(sensor_try_ms, 10000))) {
    sensor_try_ms = millis();
    Serial.println(sen66.begin() ? "SEN66 started" : "No SEN66 found");
  }
  if (sfa40.found()) {
    air::Sfa40Values f;
    if (sfa40.read(f)) {
      hcho = f.hcho;  // NAN for the first 10 minutes while it warms up
      hcho_ms = millis();
    }
  }

  air::Sen66Values s;
  if (!sen66.read(s)) return;
  sensor_seen = true;
  sensor_ms = millis();

  air::Readings r;
  r[Metric::CO2] = s.co2;
  r[Metric::PM1] = s.pm1;
  r[Metric::PM25] = s.pm25;
  r[Metric::PM10] = s.pm10;
  r[Metric::VOC] = s.voc;
  r[Metric::NOx] = s.nox;
  r[Metric::Temp] = s.temperature - settings.temperature_offset_c;
  r[Metric::Humidity] = air::compensate_humidity(s.humidity, s.temperature, settings.temperature_offset_c);
  r[Metric::HCHO] = (sfa40.found() && !elapsed(hcho_ms, 30000)) ? hcho : NAN;
  latest = r;
  history.add(millis(), r);
}

void keep_connected() {
  static bool was_connected = false;
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != was_connected) {
    Serial.println(connected ? "Wi-Fi connected, IP " + WiFi.localIP().toString() : String("Wi-Fi lost"));
    was_connected = connected;
  }
  if (!connected) {
    // No Wi-Fi to join, or a minute of failing to join it: open the setup hotspot as well
    if (!setup_mode && (!settings.have_wifi() || elapsed(wifi_begin_ms, 60UL * 1000UL))) enter_setup();
    return;
  }
  if (setup_mode && settings.have_wifi() && elapsed(setup_since, 60UL * 1000UL)) leave_setup();   // joined after all

  // Outdoor air: on connecting, then every 10 minutes (a minute after a failure)
  const unsigned long wait = (outdoor.ok ? config::kOutdoorUpdateMs : 60UL * 1000UL);
  if (!outdoor_tried || elapsed(outdoor_try_ms, wait)) {
    outdoor_tried = true;
    outdoor_try_ms = millis();
    const air::Outdoor fresh = fetch_outdoor();
    if (fresh.ok) {
      outdoor = fresh;
      outdoor_ms = millis();
    }
  }
  // Forget it if nothing new has come in for an hour
  if (outdoor.ok && elapsed(outdoor_ms, 60UL * 60UL * 1000UL)) outdoor = air::Outdoor();
}

// The history is kept against the clock so it can be carried over a restart: loaded once the
// time server has set the clock, saved every 5 minutes after that.
void keep_history() {
  const time_t t = time(nullptr);
  if (t < 1700000000) return;
  static uint8_t buf[air::History::kSaveSize];
  if (!history_restored) {
    history_restored = true;
    const size_t n = store.getBytes("history", buf, sizeof buf);
    if (n && history.restore(buf, n, (uint32_t) t, millis())) Serial.println("History restored from flash");
    history_saved_ms = millis();
  } else if (elapsed(history_saved_ms, air::History::kBucketMs)) {
    history_saved_ms = millis();
    const size_t n = history.save(buf, sizeof buf, (uint32_t) t);
    if (n) store.putBytes("history", buf, n);
  }
}

ui::ScreenData screen_data() {
  ui::ScreenData d;
  d.now = latest;
  d.history = &history;
  d.outdoor = outdoor;
  const bool sensor_ok = sensor_seen && !elapsed(sensor_ms, 30000);
  if (!sensor_ok) d.now = air::Readings();
  d.advice = air::advise(d.now, history, outdoor, sensor_seen, sensor_ok);
  const float pm25 = d.now[Metric::PM25];
  d.indoor_aqi = std::isnan(pm25) ? -1 : air::aqi_from_pm25(pm25);
  d.hcho_fitted = sfa40.found();
  d.fahrenheit = settings.fahrenheit;
  d.clock_24h = settings.clock_24h;
  d.wifi_ok = WiFi.status() == WL_CONNECTED;
  const time_t t = time(nullptr);
  d.time_valid = t > 1700000000;  // set by the time server
  if (d.time_valid) localtime_r(&t, &d.local_time);
  d.page = page;
  if (setup_mode) {
    d.advice.headline = "Set me up.";
    d.advice.detail = std::string("On your phone, join the Wi-Fi network ") + kSetupSsid + " (password " + kSetupPassword + ").";
    d.advice.outdoor = "A setup page opens. If it doesn't, go to http://" + std::string(WiFi.softAPIP().toString().c_str()) + "/";
  }
  return d;
}

void update_screen() {
  static unsigned long check_ms = 0;
  if (!redraw_now && !elapsed(check_ms, 1000)) return;
  check_ms = millis();

  const ui::ScreenData d = screen_data();
  const std::string sig = ui::signature(d);
  const bool advice_changed = d.advice.headline != shown_headline;
  if (!redraw_now && (sig == shown_signature || (!advice_changed && !elapsed(drawn_ms, config::kScreenUpdateMs)))) {
    return;
  }

  ui::draw(canvas, d);
  M5.Display.waitDisplay();
  if (clean_now || elapsed(cleaned_ms, config::kCleanRefreshMs)) {
    // Black, then white, then the picture: clears any faint ghost of earlier screens
    M5.Display.setEpdMode(epd_mode_t::epd_fast);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.waitDisplay();
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.waitDisplay();
    cleaned_ms = millis();
  }
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  canvas.pushSprite(0, 0);
  M5.Display.waitDisplay();

  shown_signature = sig;
  shown_headline = d.advice.headline;
  drawn_ms = millis();
  redraw_now = clean_now = false;
}

void check_buttons() {
  M5.update();
  static bool held = false;   // G3 held for 3 seconds opens the setup hotspot, or closes it again
  if (M5.BtnB.pressedFor(3000)) {
    if (!held) {
      held = true;
      if (setup_mode) leave_setup(); else enter_setup();
    }
  } else if (M5.BtnB.isReleased()) {
    held = false;
  }
  if (M5.BtnA.wasPressed()) {
    page = page == ui::Page::Main ? ui::Page::Trends : ui::Page::Main;
    page_ms = millis();
    redraw_now = true;
  }
  if (M5.BtnB.wasReleased() && !M5.BtnB.wasReleaseFor(3000)) redraw_now = clean_now = true;
  if (page == ui::Page::Trends && elapsed(page_ms, config::kTrendsTimeoutMs)) {
    page = ui::Page::Main;
    redraw_now = true;
  }
}

void log_readings() {
  static unsigned long log_ms = 0;
  if (!elapsed(log_ms, 60000)) return;
  log_ms = millis();
  const auto &r = latest;
  Serial.printf("CO2 %.0f  PM2.5 %.1f  VOC %.0f  NOx %.0f  HCHO %.0f  %.1f C  %.0f%%  outdoor AQI %d\n",
                r[Metric::CO2], r[Metric::PM25], r[Metric::VOC], r[Metric::NOx], r[Metric::HCHO], r[Metric::Temp],
                r[Metric::Humidity], outdoor.ok ? outdoor.aqi : -1);
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.clear_display = false;  // the first screen does a full refresh anyway
  M5.begin(cfg);
  M5.Power.M5pm1.setLedEnLevel(config::kPowerLed);  // the red LED by the power switch
  Serial.begin(115200);
  Serial.println("PaperAir starting");

  setup_display();
  store.begin("paperair");
  Serial.println(sfa40.begin() ? "SFA40 found (formaldehyde readings start after 10 minutes)" : "No SFA40");

  settings_load();
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("paperair");
  WiFi.setAutoReconnect(true);
  if (settings.have_wifi()) {
    WiFi.begin(settings.ssid.c_str(), settings.wifi_password.c_str());
    wifi_begin_ms = millis();
  } else {
    enter_setup();
  }
  configTzTime(settings.time_zone.c_str(), "pool.ntp.org", "time.google.com");

  WebHooks hooks;
  hooks.data = screen_data;
  hooks.refresh = [] { redraw_now = clean_now = true; };
  hooks.toggle_page = [] {
    page = page == ui::Page::Main ? ui::Page::Trends : ui::Page::Main;
    page_ms = millis();
    redraw_now = true;
  };
  hooks.sen66_started = [] { return sen66.started(); };
  hooks.sensor_age_ms = [] { return millis() - sensor_ms; };
  hooks.outdoor_age_ms = [] { return millis() - outdoor_ms; };
  hooks.in_setup = [] { return setup_mode; };
  hooks.enter_setup = [] { enter_setup(); };
  hooks.settings_changed = [](bool wifi_changed) {
    settings_save();
    configTzTime(settings.time_zone.c_str(), "pool.ntp.org", "time.google.com");
    if (wifi_changed) {
      // Join the new network; the setup hotspot stays up a minute in case it fails
      WiFi.mode(WIFI_AP_STA);
      WiFi.begin(settings.ssid.c_str(), settings.wifi_password.c_str());
      wifi_begin_ms = millis();
      setup_since = millis();
    }
    redraw_now = true;
  };
  web_begin(hooks);
}

void loop() {
  check_buttons();
  read_sensors();
  keep_connected();
  keep_history();
  update_screen();
  log_readings();
  if (setup_mode) {
    dns.processNextRequest();
    if (settings.have_wifi() && elapsed(setup_since, kSetupTimeoutMs)) leave_setup();
  }
  web_loop();
  delay(20);
}
