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
    case Metric::CO2: return {"CO₂", none ? "--" : air::with_commas(v), "ppm", d.co2_alerts && v > lim::kCo2Act};
    case Metric::PM1: return {"PM1", val("%.1f"), "", false};
    case Metric::PM25: return {"PM2.5", val("%.1f"), "µg/m³", v > lim::kPm25Act};
    case Metric::PM10: return {"PM10", val("%.1f"), "", v > 45};
    case Metric::VOC: return {"VOC", val("%.0f"), "", d.voc_alerts && v >= lim::kVocAct};
    case Metric::NOx: return {"NOx", val("%.0f"), "", v >= lim::kNoxAct};
    case Metric::HCHO: return {"HCHO", val("%.0f"), "ppb", v > lim::kHchoAct};
    case Metric::Temp:
      return {"Temp", none ? "--" : fmt("%.1f", temp_display(d, v)), d.fahrenheit ? "°F" : "°C", false};
    case Metric::Humidity:
      return {"Humidity", val("%.0f"), "%", v < lim::kHumidityLow || v > lim::kHumidityHigh};
    default: return {"", "", "", false};
  }
}

// ---- How each reading is plotted ---------------------------------------------
struct Plot {
  const char *unit;       // beside the big number
  float lo, hi, limit;    // chart range (it grows to fit the data), and where to act; NAN = no limit
  const char *limit_label;
  const char *note;       // under the big number when there's nothing to act on
};

Plot plot_of(Metric m) {
  switch (m) {
    case Metric::CO2: return {"ppm CO₂", 400, 1400, lim::kCo2Act, "1,000 ppm", "Open a window above 1,000"};
    case Metric::PM1: return {"µg/m³ PM1", 0, 40, NAN, "", "Very fine particles"};
    case Metric::PM25: return {"µg/m³ PM2.5", 0, 40, lim::kPm25Act, "15 µg/m³", "Run the purifier above 15"};
    case Metric::PM10: return {"µg/m³ PM10", 0, 60, 45, "45 µg/m³", "Dust and coarse particles"};
    case Metric::VOC: return {"VOC index", 0, 400, lim::kVocAct, "250", "100 is normal for this room"};
    case Metric::NOx: return {"NOx index", 0, 100, lim::kNoxAct, "50", "1 is normal"};
    case Metric::HCHO: return {"ppb formaldehyde", 0, 120, lim::kHchoAct, "80 ppb", "Air out the room above 80"};
    default: return {"", 0, 100, NAN, "", ""};
  }
}

bool plottable(const ScreenData &d, Metric m) {
  return m < Metric::Temp && (m != Metric::HCHO || d.hcho_fitted);
}

// ---- The big plot: the reading the advice is about, or the one chosen in the settings ----
struct Hero {
  Metric metric;
  std::string number, note;
};

Hero hero(const ScreenData &d) {
  const auto &a = d.advice;
  if (a.mood == Mood::Starting || a.mood == Mood::NoSensor) {
    return {plottable(d, d.big) ? d.big : Metric::CO2, "", a.mood == Mood::NoSensor ? "Check the sensor cable" : "Starting up"};
  }
  Metric m = Metric::CO2;
  std::string note;
  if (plottable(d, d.big)) {
    m = d.big;
  } else {   // automatic
    switch (a.topic) {
      case Topic::CO2:
        note = a.mood == Mood::Urgent ? "Above 1,500: ventilate now"
               : a.mood == Mood::Act  ? "Above 1,000: time to ventilate"
                                      : "Starting to build";
        break;
      case Topic::Particles:
        m = Metric::PM25;
        note = a.mood == Mood::Urgent ? "Above 35: run the purifier on high" : "Above 15: run the purifier";
        break;
      case Topic::HCHO: m = Metric::HCHO; break;
      case Topic::VOC: m = Metric::VOC; break;
      case Topic::NOx: m = Metric::NOx; break;
      default: break;
    }
    if (!plottable(d, m)) m = Metric::CO2;
  }
  if (note.empty()) note = plot_of(m).note;
  return {m, item(d, m).value, note};
}

// ---- 24-hour chart -------------------------------------------------------------
// labels: 0 none, 3 or 5 times along the bottom. hours: how far back it goes.
void chart(M5Canvas &c, const ScreenData &d, int x, int y, int w, int h, Metric m, float lo, float hi, float limit,
           const std::string &limit_label, int labels, int hours = 24) {
  const air::History &hist = *d.history;
  const int slots = std::clamp(hours, 1, 24) * 12;   // 5-minute periods
  const int n = std::min(hist.size(), slots);
  float top = hi;
  for (int age = 0; age < n; age++) {
    const float v = hist.at(m, age);
    if (!std::isnan(v)) top = std::max(top, v * 1.1f);
  }
  auto X = [&](int age) { return x + w - 1 - age * (w - 1) / (slots - 1); };
  auto Y = [&](float v) { return y + h - (int) std::lround(std::clamp((v - lo) / (top - lo), 0.0f, 1.0f) * h); };

  bool any = false;
  for (int age = 0; age < n && !any; age++) any = !std::isnan(hist.at(m, age));

  // Limit, dashed, under the data
  if (any && limit > lo && limit < top) {
    const int ly = Y(limit);
    for (int xx = x; xx < x + w; xx += 14) c.fillRect(xx, ly - 1, std::min(7, x + w - xx), 2, grey(5));
    if (!limit_label.empty()) text(c, sans_small, limit_label, x, ly - 8, 5);
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
  } else if (w >= 440) {   // the small plots just stay empty
    text(c, sans_body, "The chart fills in over the next few hours.", x + w / 2, y + h / 2, 6, textdatum_t::middle_center);
  }
  if (labels < 2) return;
  const int last = labels - 1;
  for (int k = 0; k <= last; k++) {
    const int minutes_ago = slots * 5 * (last - k) / last;
    std::string label;
    if (k == last) {
      label = "now";
    } else if (d.time_valid) {
      std::tm t = d.local_time;
      t.tm_min -= minutes_ago;
      std::mktime(&t);
      label = clock_text(d, t);
    } else {
      label = minutes_ago % 60 ? std::to_string(minutes_ago) + " min ago" : std::to_string(minutes_ago / 60) + " h ago";
    }
    const textdatum_t datum = k == 0 ? textdatum_t::baseline_left : k == last ? textdatum_t::baseline_right
                                                                              : textdatum_t::baseline_center;
    text(c, sans_small, label, x + w * k / last, y + h + 24, 6, datum);
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

// ---- Side plots and the line of everything else ------------------------------------
const Metric kPlotOrder[] = {Metric::CO2, Metric::PM25, Metric::VOC, Metric::NOx, Metric::HCHO, Metric::PM10, Metric::PM1};

// The three side readings: the ones chosen, with any that can't be shown (the same as the big
// plot, a repeat, or formaldehyde without an SFA40) replaced by the next reading not on screen
void side_metrics(const ScreenData &d, Metric big, Metric out[3]) {
  bool used[air::kMetrics] = {};
  used[(int) big] = true;
  for (int i = 0; i < 3; i++) {
    Metric m = d.side[i];
    if (!plottable(d, m) || used[(int) m]) {
      for (Metric alt : kPlotOrder)
        if (plottable(d, alt) && !used[(int) alt]) {
          m = alt;
          break;
        }
    }
    used[(int) m] = true;
    out[i] = m;
  }
}

void side_plot(M5Canvas &c, const ScreenData &d, Metric m, int x, int y, int w, int h) {
  const Item it = item(d, m);
  const Plot p = plot_of(m);
  text(c, sans_caps, upper(it.label), x, y + 18, 6);
  text(c, serif_value, it.value, x + w, y + 22, 0, textdatum_t::baseline_right);
  if (it.high) {   // past its limit
    const int vw = width(c, serif_value, it.value);
    c.fillRect(x + w - vw, y + 27, vw, 3, grey(0));
  }
  chart(c, d, x, y + 32, w, h - 32, m, p.lo, p.hi, p.limit, "", 0, d.span_hours);
}

void draw_main(M5Canvas &c, const ScreenData &d) {
  const auto &a = d.advice;
  top_line(c, d);

  // Headline: what to do, and why. Long reasons move to the smaller line below.
  const int max_w = kRight - kMargin;
  const bool two_lines = (a.topic != Topic::None || a.mood == Mood::NoSensor) &&
                         width(c, serif_head, a.detail) <= max_w;
  int y = 96;
  text(c, serif_head, a.headline, kMargin, y, 0);
  std::string sub = a.outdoor;
  if (two_lines) {
    y += 48;
    text(c, serif_head, a.detail, kMargin, y, 0);
  } else {
    sub = a.detail + (a.outdoor.empty() ? "" : " " + a.outdoor);
  }
  y += 36;
  for (const auto &line : wrap(c, sans_body, sub, max_w)) {
    text(c, sans_body, line, kMargin, y, 5);
    y += 27;
  }

  // Left: the big number and its plot. Right: three smaller plots. Bottom: everything else.
  constexpr int kBottom = 466, kSideX = 608, kMainW = 520;
  const Hero h = hero(d);
  const Plot p = plot_of(h.metric);
  const int base = y + 76;
  if (h.number.empty()) {
    text(c, sans_strong, "No readings yet", kMargin, base, 0);
  } else {
    text(c, serif_hero, h.number, kMargin - 4, base, 0);
    text(c, sans_strong, p.unit, kMargin + width(c, serif_hero, h.number) + 8, base, 0);
  }
  text(c, sans_note, h.note, kMargin, base + 30, 5);
  const int chart_y = base + 52;
  chart(c, d, kMargin, chart_y, kMainW, kBottom - chart_y, h.metric, p.lo, p.hi, p.limit, p.limit_label, 5,
        d.span_hours);

  Metric side[3];
  side_metrics(d, h.metric, side);
  const int side_top = y - 10, side_h = (kBottom + 2 - side_top) / 3;
  c.fillRect(kSideX - 24, side_top + 6, 2, kBottom + 26 - side_top - 6, grey(12));
  for (int i = 0; i < 3; i++) side_plot(c, d, side[i], kSideX, side_top + i * side_h, kRight - kSideX, side_h - 14);

  // Readings without a plot, in one line
  std::string rest;
  auto add = [&](const std::string &part) { rest += (rest.empty() ? "" : " · ") + part; };
  for (Metric m : kPlotOrder) {
    if (!plottable(d, m) || m == h.metric || m == side[0] || m == side[1] || m == side[2]) continue;
    const Item it = item(d, m);
    add(std::string(it.label) + " " + it.value + (it.unit.empty() ? "" : " " + it.unit));
  }
  if (d.indoor_aqi >= 0) add("AQI " + std::to_string(d.indoor_aqi));
  text(c, sans_small, rest, kMargin, 524, 6);
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
  chart(c, d, kMargin, y + 30, kRight - kMargin, 76, m, lo, hi, limit, limit_label, labels ? 5 : 0);
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
                  std::to_string(d.indoor_aqi) + '|' + std::to_string((int) d.big) + std::to_string((int) d.side[0]) +
                  std::to_string((int) d.side[1]) + std::to_string((int) d.side[2]) + '|' + std::to_string(d.span_hours);
  if (d.time_valid) s += '|' + std::to_string(d.local_time.tm_yday) + clock_text(d, d.local_time);
  for (int m = 0; m < air::kMetrics; m++)
    if ((Metric) m != Metric::Temp && (Metric) m != Metric::Humidity) s += '|' + item(d, (Metric) m).value;
  return s;
}

}  // namespace ui
