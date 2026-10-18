#include "screen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "aqi.h"
#include "fonts/fonts.h"

namespace ui {
namespace {

using air::Metric;
using air::Mood;
using air::Topic;
namespace lim = air::limits;

constexpr int kMargin = 40;
constexpr int kRight = kWidth - kMargin;

// The panel shows 16 greys: 0 is black, 15 is white
uint32_t grey(int n) {
  const uint8_t v = (uint8_t) (std::clamp(n, 0, 15) * 17);
  return lgfx::color888(v, v, v);
}

void text(M5Canvas &c, const uint8_t *font, const std::string &s, int x, int y, int g,
          textdatum_t datum = textdatum_t::baseline_left) {
  c.loadFont(font);
  c.setTextColor(grey(g));
  c.setTextDatum(datum);
  c.drawString(s.c_str(), x, y);
}

int width(M5Canvas &c, const uint8_t *font, const std::string &s) {
  c.loadFont(font);
  return c.textWidth(s.c_str());
}

std::string upper(std::string s) {
  for (char &ch : s)
    if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
  return s;
}

std::string fmt(const char *f, float v) {
  char buf[48];
  snprintf(buf, sizeof(buf), f, v);
  return buf;
}

// Splits text into lines no wider than max_w
std::vector<std::string> wrap(M5Canvas &c, const uint8_t *font, const std::string &s, int max_w) {
  std::vector<std::string> lines;
  std::string line;
  size_t i = 0;
  while (i <= s.size()) {
    size_t j = s.find(' ', i);
    if (j == std::string::npos) j = s.size();
    std::string word = s.substr(i, j - i);
    std::string trial = line.empty() ? word : line + " " + word;
    if (!line.empty() && width(c, font, trial) > max_w) {
      lines.push_back(line);
      line = word;
    } else {
      line = trial;
    }
    i = j + 1;
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}

std::string clock_text(const ScreenData &d, const std::tm &t) {
  char buf[16];
  if (d.clock_24h) {
    strftime(buf, sizeof(buf), "%H:%M", &t);
  } else {
    int h = t.tm_hour % 12;
    snprintf(buf, sizeof(buf), "%d:%02d %s", h == 0 ? 12 : h, t.tm_min, t.tm_hour < 12 ? "am" : "pm");
  }
  return buf;
}

float temp_display(const ScreenData &d, float c) { return d.fahrenheit ? c * 9 / 5 + 32 : c; }

// ---- Readings: how each one is labelled and formatted -----------------------
struct Item {
  const char *label;
  std::string value, unit;
  bool high;
};

Item item(const ScreenData &d, Metric m) {
  const float v = d.now[m];
  const bool none = std::isnan(v);
  auto val = [&](const char *f) { return none ? std::string("--") : fmt(f, v); };
  switch (m) {
    case Metric::CO2: return {"CO₂", none ? "--" : air::with_commas(v), "ppm", v > lim::kCo2Act};
    case Metric::PM1: return {"PM1", val("%.1f"), "", false};
    case Metric::PM25: return {"PM2.5", val("%.1f"), "µg/m³", v > lim::kPm25Act};
    case Metric::PM10: return {"PM10", val("%.1f"), "", v > 45};
    case Metric::VOC: return {"VOC", val("%.0f"), "", v >= lim::kVocAct};
    case Metric::NOx: return {"NOx", val("%.0f"), "", v >= lim::kNoxAct};
    case Metric::HCHO: return {"HCHO", val("%.0f"), "ppb", v > lim::kHchoAct};
    case Metric::Temp:
      return {"Temp", none ? "--" : fmt("%.1f", temp_display(d, v)), d.fahrenheit ? "°F" : "°C", false};
    case Metric::Humidity:
      return {"Humidity", val("%.0f"), "%", v < lim::kHumidityLow || v > lim::kHumidityHigh};
    default: return {"", "", "", false};
  }
}

// ---- The featured reading ----------------------------------------------------
struct Hero {
  std::string number, unit, note;
  Metric chart;  // which history the chart shows
  float lo, hi, limit;
  std::string limit_label;
};

Hero hero(const ScreenData &d) {
  const auto &a = d.advice;
  const auto &r = d.now;
  auto num = [](float v, const char *f) { return std::isnan(v) ? std::string("--") : fmt(f, v); };
  if (a.mood == Mood::Starting || a.mood == Mood::NoSensor) {
    return {"", "No readings yet", a.mood == Mood::NoSensor ? "Check the sensor cable" : "Starting up",
            Metric::CO2, 400, 1400, lim::kCo2Act, "1,000 ppm"};
  }
  switch (a.topic) {
    case Topic::CO2:
      return {air::with_commas(r[Metric::CO2]), "ppm CO₂",
              a.mood == Mood::Urgent ? "Above 1,500: ventilate now"
              : a.mood == Mood::Act  ? "Above 1,000: time to ventilate"
                                     : "Open a window above 1,000",
              Metric::CO2, 400, 1400, lim::kCo2Act, "1,000 ppm"};
    case Topic::Particles:
      return {num(r[Metric::PM25], "%.1f"), "µg/m³ PM2.5",
              a.mood == Mood::Urgent ? "Above 35: run the purifier on high" : "Above 15: run the purifier",
              Metric::PM25, 0, 40, lim::kPm25Act, "15 µg/m³"};
    case Topic::HCHO:
      return {num(r[Metric::HCHO], "%.0f"), "ppb formaldehyde", "Above 80: air out the room", Metric::HCHO, 0, 120,
              lim::kHchoAct, "80 ppb"};
    case Topic::VOC:
      return {num(r[Metric::VOC], "%.0f"), "VOC index", "100 is normal for this room", Metric::VOC, 0, 400,
              lim::kVocAct, "250"};
    case Topic::NOx:
      return {num(r[Metric::NOx], "%.0f"), "NOx index", "1 is normal", Metric::NOx, 0, 100, lim::kNoxAct, "50"};
    case Topic::Dry:
    case Topic::Humid:
      return {num(r[Metric::Humidity], "%.0f") + "%", "humidity", "30 to 60% is comfortable", Metric::Humidity, 0,
              100, a.topic == Topic::Dry ? lim::kHumidityLow : lim::kHumidityHigh,
              a.topic == Topic::Dry ? "30%" : "60%"};
    default: {
      // Nothing to act on: show comfort, with CO2 through the day in the chart
      const float t = temp_display(d, r[Metric::Temp]);
      const float rh = r[Metric::Humidity];
      std::string note = "Comfortable";
      const float t_f = d.fahrenheit ? t : t * 9 / 5 + 32;
      if (!std::isnan(t_f) && t_f < 66) note = "A little cool";
      if (!std::isnan(t_f) && t_f > 78) note = "A little warm";
      return {std::isnan(t) ? "--" : fmt("%.0f", t) + "°", std::isnan(rh) ? "humidity --" : fmt("%.0f", rh) + "% humidity",
              note, Metric::CO2, 400, 1400, lim::kCo2Act, "CO₂ · open a window above 1,000 ppm"};
    }
  }
}

// ---- 24-hour chart -------------------------------------------------------------
void chart(M5Canvas &c, const ScreenData &d, int x, int y, int w, int h, Metric m, float lo, float hi, float limit,
           const std::string &limit_label, bool time_labels) {
  const air::History &hist = *d.history;
  const int n = std::min(hist.size(), air::History::kSlots);
  float top = hi;
  for (int age = 0; age < n; age++) {
    const float v = hist.at(m, age);
    if (!std::isnan(v)) top = std::max(top, v * 1.1f);
  }
  auto X = [&](int age) { return x + w - 1 - age * (w - 1) / (air::History::kSlots - 1); };
  auto Y = [&](float v) { return y + h - (int) std::lround(std::clamp((v - lo) / (top - lo), 0.0f, 1.0f) * h); };

  bool any = false;
  for (int age = 0; age < n && !any; age++) any = !std::isnan(hist.at(m, age));

  // Limit, dashed, under the data
  if (any && limit > lo && limit < top) {
    const int ly = Y(limit);
    for (int xx = x; xx < x + w; xx += 14) c.fillRect(xx, ly - 1, std::min(7, x + w - xx), 2, grey(5));
    text(c, sans_small, limit_label, x, ly - 8, 5);
  }
  // Shaded area and line, broken where there were no readings
  int valid = 0, px = 0, py = 0, ex = -1, ey = -1;
  for (int age = n - 1; age >= 0; age--) {
    const float v = hist.at(m, age);
    if (std::isnan(v)) {
      valid = 0;
      continue;
    }
    const int cx = X(age), cy = Y(v);
    if (valid) {
      for (int xi = px; xi <= cx; xi++) {
        const int yi = py + (cy - py) * (xi - px) / std::max(1, cx - px);
        c.drawFastVLine(xi, yi, y + h - yi, grey(12));
      }
      c.drawWideLine(px, py, cx, cy, 1.6f, grey(2));
    }
    px = cx;
    py = cy;
    ex = cx;
    ey = cy;
    valid++;
  }
  c.fillRect(x, y + h, w, 2, grey(8));
  if (ex >= 0) {
    c.fillSmoothCircle(ex, ey, 9, grey(15));
    c.fillSmoothCircle(ex, ey, 7, grey(0));
  } else {
    text(c, sans_body, "The chart fills in over the next few hours.", x + w / 2, y + h / 2, 6, textdatum_t::middle_center);
  }
  if (!time_labels) return;
  for (int k = 0; k <= 4; k++) {
    std::string label;
    if (k == 4) {
      label = "now";
    } else if (d.time_valid) {
      std::tm t = d.local_time;
      t.tm_hour -= 24 - 6 * k;
      std::mktime(&t);
      label = clock_text(d, t);
    } else {
      label = std::to_string(24 - 6 * k) + " h ago";
    }
    const textdatum_t datum = k == 0 ? textdatum_t::baseline_left : k == 4 ? textdatum_t::baseline_right
                                                                           : textdatum_t::baseline_center;
    text(c, sans_small, label, x + w * k / 4, y + h + 24, 6, datum);
  }
}

// ---- Top line: date and time, outdoor air ------------------------------------
void top_line(M5Canvas &c, const ScreenData &d) {
  std::string left = "Starting";
  if (d.time_valid) {
    char buf[48];
    strftime(buf, sizeof(buf), "%A %e %B", &d.local_time);
    std::string date = buf;
    date.erase(std::unique(date.begin(), date.end(), [](char a, char b) { return a == ' ' && b == ' '; }), date.end());
    left = date + " · " + clock_text(d, d.local_time);
  }
  text(c, sans_caps, upper(left), kMargin, 38, 6);
  std::string right;
  if (!d.wifi_ok) {
    right = "No Wi-Fi";
  } else if (d.outdoor.ok) {
    right = "Outside AQI " + std::to_string(d.outdoor.aqi) + " · " + air::aqi_word(d.outdoor.aqi);
  } else {
    right = "Outside --";
  }
  text(c, sans_caps, upper(right), kRight, 38, 6, textdatum_t::baseline_right);
}

// ---- Bottom row: every other reading ---------------------------------------------
void bottom_row(M5Canvas &c, const ScreenData &d, Metric featured, bool featured_is_comfort) {
  std::vector<Item> items;
  const Metric order[] = {Metric::Temp, Metric::Humidity, Metric::CO2, Metric::PM25, Metric::PM1,
                          Metric::PM10, Metric::VOC,      Metric::NOx, Metric::HCHO};
  for (Metric m : order) {
    if (featured_is_comfort ? (m == Metric::Temp || m == Metric::Humidity) : m == featured) continue;
    if (m == Metric::HCHO && !d.hcho_fitted) continue;
    items.push_back(item(d, m));
  }
  if (items.size() < 8 && d.indoor_aqi >= 0) items.push_back({"AQI", std::to_string(d.indoor_aqi), "", false});
  if (items.size() > 8) items.resize(8);

  const int top = 454;
  c.fillRect(kMargin, top, kRight - kMargin, 2, grey(11));
  std::vector<int> widths;
  int total = 0;
  for (const auto &it : items) {
    int wv = width(c, serif_value, it.value) + (it.unit.empty() ? 0 : 4 + width(c, sans_small, it.unit));
    int wl = width(c, sans_caps, upper(it.label));
    widths.push_back(std::max(wv, wl));
    total += widths.back();
  }
  const int gap = items.size() > 1 ? (kRight - kMargin - total) / (int) (items.size() - 1) : 0;
  int x = kMargin;
  for (size_t i = 0; i < items.size(); i++) {
    const auto &it = items[i];
    text(c, sans_caps, upper(it.label), x, top + 26, 6);
    text(c, serif_value, it.value, x, top + 58, 0);
    const int vw = width(c, serif_value, it.value);
    if (!it.unit.empty()) text(c, sans_small, it.unit, x + vw + 4, top + 58, 6);
    if (it.high) c.fillRect(x, top + 64, vw, 3, grey(0));  // past its limit
    x += widths[i] + gap;
  }
}

void draw_main(M5Canvas &c, const ScreenData &d) {
  const auto &a = d.advice;
  top_line(c, d);

  // Headline: what to do, and why. Long reasons move to the smaller line below.
  const int max_w = kRight - kMargin;
  const bool two_lines = (a.topic != Topic::None || a.mood == Mood::NoSensor) &&
                         width(c, serif_head, a.detail) <= max_w;
  int y = 100;
  text(c, serif_head, a.headline, kMargin, y, 0);
  std::string sub = a.outdoor;
  if (two_lines) {
    y += 48;
    text(c, serif_head, a.detail, kMargin, y, 0);
  } else {
    sub = a.detail + (a.outdoor.empty() ? "" : " " + a.outdoor);
  }
  y += 40;
  for (const auto &line : wrap(c, sans_body, sub, max_w)) {
    text(c, sans_body, line, kMargin, y, 5);
    y += 27;
  }

  // The featured reading and its day
  const Hero h = hero(d);
  const int base = h.number.empty() ? 250 : 312;  // no number yet: lift the words beside the chart
  text(c, serif_hero, h.number, kMargin - 4, base, 0);
  text(c, sans_strong, h.unit, kMargin, base + 36, 0);
  text(c, sans_note, h.note, kMargin, base + 60, 5);
  const bool comfort = a.topic == Topic::None || a.topic == Topic::OutdoorSmoke;
  chart(c, d, 360, std::max(y + 18, 226), 560, 150, h.chart, h.lo, h.hi, h.limit, h.limit_label, true);

  bottom_row(c, d, h.chart, comfort || a.mood == Mood::Starting || a.mood == Mood::NoSensor);
}

void trend_block(M5Canvas &c, const ScreenData &d, int y, const char *title, const char *unit, Metric m, float lo,
                 float hi, float limit, const std::string &limit_label, const char *f, bool labels) {
  const air::History &hist = *d.history;
  float now = NAN, mn = NAN, mx = NAN;
  for (int age = 0; age < hist.size(); age++) {
    const float v = hist.at(m, age);
    if (std::isnan(v)) continue;
    if (std::isnan(now)) now = v;
    mn = std::isnan(mn) ? v : std::min(mn, v);
    mx = std::isnan(mx) ? v : std::max(mx, v);
  }
  text(c, sans_strong, title, kMargin, y, 0);
  text(c, sans_caps, "LAST 24 HOURS", kMargin + width(c, sans_strong, title) + 10, y, 6);
  if (!std::isnan(now)) {
    auto v = [&](float x) { return m == Metric::CO2 ? air::with_commas(x) : fmt(f, x); };
    text(c, sans_small, "now " + v(now) + " · low " + v(mn) + " · high " + v(mx) + " " + unit, kRight, y, 5,
         textdatum_t::baseline_right);
  }
  chart(c, d, kMargin, y + 30, kRight - kMargin, 76, m, lo, hi, limit, limit_label, labels);
}

void draw_trends(M5Canvas &c, const ScreenData &d) {
  top_line(c, d);
  trend_block(c, d, 92, "CO₂", "ppm", Metric::CO2, 400, 1400, lim::kCo2Act, "1,000 ppm", "%.0f", false);
  trend_block(c, d, 232, "Fine particles", "µg/m³ PM2.5", Metric::PM25, 0, 30, lim::kPm25Act, "15 µg/m³", "%.1f",
              false);
  if (d.hcho_fitted) {
    trend_block(c, d, 372, "Formaldehyde", "ppb", Metric::HCHO, 0, 100, lim::kHchoAct, "80 ppb", "%.0f", true);
  } else {
    trend_block(c, d, 372, "VOC index", "", Metric::VOC, 0, 300, lim::kVocAct, "250", "%.0f", true);
  }
}

}  // namespace

void draw(M5Canvas &c, const ScreenData &d) {
  c.fillScreen(grey(15));
  if (d.page == Page::Trends) {
    draw_trends(c, d);
  } else {
    draw_main(c, d);
  }
  c.unloadFont();
}

std::string signature(const ScreenData &d) {
  std::string s = std::to_string((int) d.page) + '|' + (d.wifi_ok ? "w" : "-") + '|' +
                  std::to_string(d.outdoor.ok ? d.outdoor.aqi : -1) + '|' + d.advice.headline + '|' + d.advice.detail +
                  '|' + d.advice.outdoor + '|' + std::to_string(d.history ? d.history->version() : 0) + '|' +
                  std::to_string(d.indoor_aqi);
  if (d.time_valid) s += '|' + std::to_string(d.local_time.tm_yday) + clock_text(d, d.local_time);
  for (int m = 0; m < air::kMetrics; m++) s += '|' + item(d, (Metric) m).value;
  return s;
}

}  // namespace ui
