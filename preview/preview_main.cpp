// Desktop preview: draws each screen state to preview/<name>.png with the same code the device runs.
//   pio run -e preview -t exec
// The images are reduced to the panel's 16 greys, so they look like the real thing.
#include <M5GFX.h>

#include <cmath>
#include <cstdio>
#include <functional>

#include "advice.h"
#include "aqi.h"
#include "readings.h"
#include "screen.h"

using air::Metric;

namespace {

// A day in a living room: CO2 rises overnight with the door shut, drops when a window opens in the
// morning, and climbs again through the afternoon. `hours_ago` runs 24 -> 0.
struct Day {
  float co2_now = 1180, pm25_now = 6.2f, hcho_now = 22, voc_now = 118, temp_now = 21.8f, rh_now = 44;
  bool cooking = false;  // a burst of particles in the last hour
};

air::Readings sample(const Day &day, float hours_ago) {
  air::Readings r;
  const float t = 24 - hours_ago;  // hours since the start of the chart
  const float night = std::exp(-std::pow((t - 8) / 4.5f, 2));
  const float build = std::clamp((t - 18.5f) / 5.5f, 0.0f, 1.0f);
  const float wobble = std::sin(t * 2.3f) * 0.5f + std::sin(t * 5.1f) * 0.5f;
  r[Metric::CO2] = 560 + 480 * night + (day.co2_now - 560) * build * build + 15 * wobble;
  float pm = 4 + 2 * night + 0.6f * wobble;
  pm += (day.pm25_now - 4) * (day.cooking ? std::exp(-std::pow(hours_ago / 0.8f, 2)) : build);
  r[Metric::PM25] = std::max(0.5f, pm);
  r[Metric::PM1] = r[Metric::PM25] * 0.8f;
  r[Metric::PM10] = r[Metric::PM25] * 1.3f;
  r[Metric::VOC] = 100 + (day.voc_now - 100) * build + 10 * wobble;
  r[Metric::NOx] = 1;
  r[Metric::HCHO] = 14 + (day.hcho_now - 14) * build + 2 * wobble;
  r[Metric::Temp] = day.temp_now - 1.5f * night + 0.2f * wobble;
  r[Metric::Humidity] = day.rh_now + 3 * night + wobble;
  return r;
}

void fill_history(air::History &h, const Day &day) {
  for (int age = air::History::kSlots - 1; age >= 0; age--) h.push_for_preview(sample(day, age / 12.0f));
}

// Panel greys: 0, 17, 34 ... 255
void quantize(M5Canvas &c) {
  for (int y = 0; y < c.height(); y++) {
    for (int x = 0; x < c.width(); x++) {
      const uint8_t v = c.readPixelRGB(x, y).R8();
      const uint8_t q = (uint8_t) (std::lround(v / 17.0f) * 17);
      if (q != v) c.drawPixel(x, y, lgfx::color888(q, q, q));
    }
  }
}

void save(M5Canvas &c, const char *name) {
  quantize(c);
  size_t len = 0;
  void *png = c.createPng(&len, 0, 0, c.width(), c.height());
  char path[128];
  snprintf(path, sizeof(path), "preview/%s.png", name);
  FILE *f = fopen(path, "wb");
  if (!f || !png) {
    fprintf(stderr, "could not write %s\n", path);
    return;
  }
  fwrite(png, 1, len, f);
  fclose(f);
  free(png);
  printf("wrote %s\n", path);
}

void render(M5Canvas &c, const char *name, const Day &day, const air::Outdoor &out,
            std::function<void(ui::ScreenData &, air::Readings &)> tweak = nullptr, bool hcho = true) {
  static air::History h;
  h = air::History();
  fill_history(h, day);
  ui::ScreenData d;
  d.now = sample(day, 0);
  if (!hcho) d.now[Metric::HCHO] = NAN;
  d.hcho_fitted = hcho;
  d.history = &h;
  d.outdoor = out;
  d.time_valid = true;
  d.local_time.tm_year = 2026 - 1900;
  d.local_time.tm_mon = 9;
  d.local_time.tm_mday = 18;
  d.local_time.tm_hour = 14;
  d.local_time.tm_min = 32;
  std::mktime(&d.local_time);
  bool sensor_ok = true;
  if (tweak) tweak(d, d.now);
  if (std::isnan(d.now[Metric::CO2]) && std::isnan(d.now[Metric::PM25])) sensor_ok = false;
  d.indoor_aqi = std::isnan(d.now[Metric::PM25]) ? -1 : air::aqi_from_pm25(d.now[Metric::PM25]);
  d.advice = air::advise(d.now, h, d.outdoor, sensor_ok, sensor_ok);
  ui::draw(c, d);
  save(c, name);
}

}  // namespace

int main() {
  M5Canvas c;
  c.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  if (!c.createSprite(ui::kWidth, ui::kHeight)) {
    fprintf(stderr, "could not create the canvas\n");
    return 1;
  }
  air::Outdoor clean{true, 26, 6.1f};
  air::Outdoor smoky{true, 168, 88};

  render(c, "co2", Day{}, clean);
  render(c, "fresh", Day{.co2_now = 640, .pm25_now = 3.4f, .hcho_now = 16, .voc_now = 96}, clean);
  render(c, "particles", Day{.co2_now = 720, .pm25_now = 24, .voc_now = 160, .cooking = true}, clean);
  render(c, "smoke_outside", Day{.co2_now = 690, .pm25_now = 7}, smoky);
  render(c, "dry", Day{.co2_now = 610, .pm25_now = 3, .temp_now = 20.5f, .rh_now = 24}, clean, nullptr, false);
  render(c, "starting", Day{}, clean, [](ui::ScreenData &d, air::Readings &r) {
    r = air::Readings();
    static air::History empty;
    d.history = &empty;
    d.indoor_aqi = -1;
  });
  render(c, "trends", Day{}, clean, [](ui::ScreenData &d, air::Readings &) { d.page = ui::Page::Trends; });
  return 0;
}
