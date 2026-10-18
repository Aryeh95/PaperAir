#pragma once
// What the screen tells you to do, in plain words.
//
// Limits (all in one place so they're easy to change):
//   CO2         800 ppm starting to build, 1000 open a window, 1500 urgent (German Federal
//               Environment Agency guidance: under 1000 ppm is fine, above 2000 is unacceptable)
//   PM2.5       9 ug/m3 above usual (top of the EPA "good" range), 15 act (WHO 2021 24-hour
//               guideline), 35.5 urgent (EPA "unhealthy for sensitive groups")
//   HCHO        40 ppb watch (Health Canada long-term limit, 50 ug/m3), 80 act (WHO, 0.1 mg/m3)
//   VOC index   150 watch, 250 act (Sensirion's index: 100 is the room's normal level)
//   NOx index   20 watch, 50 act (1 is normal; a gas stove pushes it up)
//   Humidity    30-60% comfortable
#include <cmath>
#include <cstdio>
#include <string>

#include "aqi.h"
#include "readings.h"

namespace air {

namespace limits {
constexpr float kCo2Watch = 800, kCo2Act = 1000, kCo2Urgent = 1500;
constexpr float kPm25Watch = 9, kPm25Act = 15, kPm25Urgent = 35.5f;
constexpr float kHchoWatch = 40, kHchoAct = 80;
constexpr float kVocWatch = 150, kVocAct = 250;
constexpr float kNoxWatch = 20, kNoxAct = 50;
constexpr float kHumidityLow = 30, kHumidityHigh = 60;
constexpr float kOutdoorClean = 15;   // ug/m3 PM2.5 outside: airing out is fine
constexpr float kOutdoorSmoky = 35.5f;  // keep windows shut
}  // namespace limits

// How the screen should feel
enum class Mood {
  Starting,  // no readings yet
  NoSensor,  // the SEN66 isn't answering
  Fresh,     // nothing to do
  Fine,      // worth knowing, nothing urgent
  Act,       // do something
  Urgent,    // do something now
};

// What the advice is about; the screen features this reading
enum class Topic { None, CO2, Particles, VOC, NOx, HCHO, Dry, Humid, OutdoorSmoke };

struct Outdoor {
  bool ok = false;  // a recent reading is available
  int aqi = -1;
  float pm25 = NAN;
};

struct Advice {
  Mood mood = Mood::Starting;
  Topic topic = Topic::None;
  std::string headline;  // "Open a window."
  std::string detail;    // "CO₂ has been building for about 3 hours."
  std::string outdoor;   // "Outside is clean (AQI 26), so it's a good time to air out."
};

// "1,180"
inline std::string with_commas(float v) {
  long n = std::lround(v);
  char buf[24];
  snprintf(buf, sizeof(buf), "%ld", n < 0 ? -n : n);
  std::string s(buf), out;
  int lead = (int) s.size() % 3;
  for (size_t i = 0; i < s.size(); i++) {
    if (i && (int) (i - lead) % 3 == 0) out += ',';
    out += s[i];
  }
  return (n < 0 ? "-" : "") + out;
}

// "40 minutes", "about 3 hours"
inline std::string duration_words(int minutes) {
  char buf[32];
  if (minutes < 60) {
    snprintf(buf, sizeof(buf), "%d minutes", minutes);
  } else {
    const int hours = (int) std::lround(minutes / 60.0);
    snprintf(buf, sizeof(buf), "about %d hour%s", hours, hours == 1 ? "" : "s");
  }
  return buf;
}

inline std::string fmt(const char *format, float v) {
  char buf[96];
  snprintf(buf, sizeof(buf), format, v);
  return buf;
}

// sensor_seen: the SEN66 has reported at least once; sensor_ok: it's reporting now.
inline Advice advise(const Readings &r, const History &h, const Outdoor &out, bool sensor_seen, bool sensor_ok) {
  using namespace limits;
  Advice a;
  const float co2 = r[Metric::CO2], pm25 = r[Metric::PM25], voc = r[Metric::VOC], nox = r[Metric::NOx];
  const float hcho = r[Metric::HCHO], rh = r[Metric::Humidity];

  if (!sensor_ok) {
    if (sensor_seen) {
      a = {Mood::NoSensor, Topic::None, "The sensor stopped answering.", "Check the SEN66 cable.", ""};
    } else {
      a = {Mood::Starting, Topic::None, "Warming up.", "The first readings take a minute or two.", ""};
    }
    return a;
  }
  if (std::isnan(co2) || std::isnan(pm25)) {
    a = {Mood::Starting, Topic::None, "Warming up.", "The first readings take a minute or two.", ""};
    return a;
  }

  const bool outdoor_known = out.ok && !std::isnan(out.pm25);
  const bool outdoor_clean = outdoor_known && out.pm25 <= kOutdoorClean;
  const bool outdoor_smoky = outdoor_known && out.pm25 >= kOutdoorSmoky;
  char aqi_txt[24] = "";
  if (outdoor_known) snprintf(aqi_txt, sizeof(aqi_txt), "AQI %d", out.aqi);

  // Ventilating helps with gases; outside air decides how much
  auto ventilate = [&](Mood mood, Topic topic, const char *headline, const std::string &detail) {
    a.mood = mood;
    a.topic = topic;
    a.detail = detail;
    if (outdoor_smoky) {
      a.headline = "Air out briefly.";
      a.outdoor = std::string("Outside is ") + aqi_word(out.aqi) + " (" + aqi_txt + "). Close the windows again after a few minutes.";
    } else {
      a.headline = headline;
      if (outdoor_clean) {
        a.outdoor = std::string("Outside is clean (") + aqi_txt + "), so it's a good time to air out.";
      } else if (outdoor_known) {
        a.outdoor = std::string("Outside is ") + aqi_word(out.aqi) + " (" + aqi_txt + "). A short airing is fine.";
      }
    }
  };
  auto co2_detail = [&]() {
    const int minutes = h.minutes_above(Metric::CO2, kCo2Watch);
    if (minutes >= 30) return "CO₂ has been building for " + duration_words(minutes) + ".";
    return "CO₂ is at " + with_commas(co2) + " ppm.";
  };
  auto particles = [&](Mood mood) {
    a.mood = mood;
    a.topic = Topic::Particles;
    a.headline = "Run the air purifier.";
    a.detail = (mood == Mood::Urgent ? "Lots of fine particles indoors (" : "Fine particles are up (") +
               fmt("%.0f", pm25) + " µg/m³).";
    if (outdoor_known && out.pm25 < pm25 && !outdoor_smoky) {
      a.outdoor = std::string("Outside is cleaner (") + aqi_txt + "), so opening a window helps too.";
    } else if (outdoor_known) {
      a.outdoor = std::string("Outside is ") + aqi_word(out.aqi) + " (" + aqi_txt + "). Keep the windows shut.";
    }
  };

  // Most urgent first
  if (co2 > kCo2Urgent) {
    ventilate(Mood::Urgent, Topic::CO2, "Open a window now.", "CO₂ is very high: " + with_commas(co2) + " ppm.");
  } else if (pm25 >= kPm25Urgent) {
    particles(Mood::Urgent);
  } else if (hcho > kHchoAct) {
    ventilate(Mood::Act, Topic::HCHO, "Air out the room.", "Formaldehyde is high: " + fmt("%.0f", hcho) + " ppb.");
  } else if (pm25 > kPm25Act) {
    particles(Mood::Act);
  } else if (voc >= kVocAct) {
    ventilate(Mood::Act, Topic::VOC, "Air out the room.", "Strong smells or fumes (VOC index " + fmt("%.0f", voc) + ").");
  } else if (nox >= kNoxAct) {
    ventilate(Mood::Act, Topic::NOx, "Turn on the kitchen fan.", "Combustion gases are up, often from a gas stove (NOx index " + fmt("%.0f", nox) + ").");
  } else if (co2 > kCo2Act) {
    ventilate(Mood::Act, Topic::CO2, "Open a window.", co2_detail());
  } else if (outdoor_known && out.aqi > 150) {
    a = {Mood::Fine, Topic::OutdoorSmoke, "Keep the windows shut.",
         std::string("It's ") + aqi_word(out.aqi) + " outside (" + aqi_txt + "). The air inside is fine.", ""};
  } else if (!std::isnan(rh) && rh < kHumidityLow) {
    a = {Mood::Fine, Topic::Dry, "The air is dry.", "Humidity is " + fmt("%.0f", rh) + "%. A humidifier would help.", ""};
  } else if (!std::isnan(rh) && rh > kHumidityHigh) {
    ventilate(Mood::Fine, Topic::Humid, "The air is humid.", "Humidity is " + fmt("%.0f", rh) + "%.");
  } else if (co2 > kCo2Watch) {
    a = {Mood::Fine, Topic::CO2, "The air is fine.", "CO₂ is starting to build (" + with_commas(co2) + " ppm).", ""};
  } else if (pm25 > kPm25Watch || hcho > kHchoWatch || voc >= kVocWatch || nox >= kNoxWatch) {
    a = {Mood::Fine, Topic::None, "The air is fine.", "A little above its usual level, nothing to do yet.", ""};
  } else {
    a = {Mood::Fresh, Topic::None, "The air is fresh.", "Nothing to do.", ""};
  }

  // Nothing to act on: just say what it's like outside (its AQI is already at the top of the screen)
  if (a.outdoor.empty() && outdoor_known && a.topic != Topic::OutdoorSmoke) {
    if (a.mood == Mood::Fresh || a.mood == Mood::Fine) {
      a.outdoor = outdoor_clean ? std::string("Outside is clean too.") : std::string("Outside is ") + aqi_word(out.aqi) + ".";
    }
  }
  return a;
}

}  // namespace air
