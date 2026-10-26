// PaperAir: an air quality display for the M5Stack PaperDIY.
//
// Reads the SEN66 (and an SFA40 if there is one) every second, fetches outdoor air every 10
// minutes, and redraws the e-paper screen when something worth showing has changed.
// Buttons: G4 switches between the main page and the 24-hour trends; G3 does a full refresh.
#include <M5Unified.h>
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

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy include/secrets.example.h to include/secrets.h and fill it in"
#endif

using air::Metric;

namespace {

M5Canvas canvas(&M5.Display);
Sen66 sen66(m5::In_I2C);  // not M5.In_I2C: that reference is only set once M5 is constructed,
                           // which may be after these globals
Sfa40 sfa40(m5::In_I2C);

air::History history;
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
  r[Metric::Temp] = s.temperature - config::kTemperatureOffsetC;
  r[Metric::Humidity] = air::compensate_humidity(s.humidity, s.temperature, config::kTemperatureOffsetC);
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
  if (!connected) return;

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
  d.fahrenheit = config::kFahrenheit;
  d.clock_24h = config::kClock24h;
  d.wifi_ok = WiFi.status() == WL_CONNECTED;
  const time_t t = time(nullptr);
  d.time_valid = t > 1700000000;  // set by the time server
  if (d.time_valid) localtime_r(&t, &d.local_time);
  d.page = page;
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
  if (M5.BtnA.wasPressed()) {
    page = page == ui::Page::Main ? ui::Page::Trends : ui::Page::Main;
    page_ms = millis();
    redraw_now = true;
  }
  if (M5.BtnB.wasPressed()) redraw_now = clean_now = true;
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
  Serial.begin(115200);
  Serial.println("PaperAir starting");

  setup_display();
  Serial.println(sfa40.begin() ? "SFA40 found (formaldehyde readings start after 10 minutes)" : "No SFA40");

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("paperair");
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  configTzTime(config::kTimeZone, "pool.ntp.org", "time.google.com");
}

void loop() {
  check_buttons();
  read_sensors();
  keep_connected();
  update_screen();
  log_readings();
  delay(20);
}
