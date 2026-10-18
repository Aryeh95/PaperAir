#pragma once
// One set of indoor readings, and a 24-hour history of them.
#include <cmath>
#include <cstdint>

namespace air {

enum class Metric : uint8_t { CO2, PM1, PM25, PM10, VOC, NOx, HCHO, Temp, Humidity, Count };
constexpr int kMetrics = (int) Metric::Count;

// Units: CO2 ppm, PM ug/m3, VOC and NOx Sensirion index, HCHO ppb, Temp degrees C, Humidity %.
// NAN means no reading (sensor missing or still warming up).
struct Readings {
  float v[kMetrics];
  Readings() {
    for (float &x : v) x = NAN;
  }
  float &operator[](Metric m) { return v[(int) m]; }
  float operator[](Metric m) const { return v[(int) m]; }
};

// 24 hours of 5-minute averages, for the charts and for "building for 3 hours".
// Kept in RAM: it starts again after a restart.
class History {
 public:
  static constexpr uint32_t kBucketMs = 5UL * 60UL * 1000UL;
  static constexpr int kSlots = 288;

  void add(uint32_t now_ms, const Readings &r) {
    const uint32_t bucket = now_ms / kBucketMs;
    if (!open_) {
      open_bucket_(bucket);
    } else if (bucket != bucket_id_) {
      close_bucket_();
      uint32_t gap = bucket - bucket_id_ - 1;  // whole buckets without any readings
      if (gap > (uint32_t) kSlots) gap = 0;    // millis() wrapped (every ~49 days)
      for (uint32_t i = 0; i < gap; i++) push_(Readings());
      open_bucket_(bucket);
    }
    for (int m = 0; m < kMetrics; m++) {
      if (!std::isnan(r.v[m])) {
        sum_[m] += r.v[m];
        n_[m]++;
      }
    }
  }

  // Points available, newest first: age 0 is the current, unfinished 5 minutes
  int size() const { return size_ + (open_ ? 1 : 0); }

  float at(Metric m, int age) const {
    const int i = (int) m;
    if (open_) {
      if (age == 0) return n_[i] ? sum_[i] / n_[i] : NAN;
      age--;
    }
    if (age >= size_) return NAN;
    return data_[(head_ + kSlots - 1 - age) % kSlots].v[i];
  }

  // How long the metric has been above `level`, up to now, in minutes (0 if it isn't now)
  int minutes_above(Metric m, float level) const {
    int n = 0;
    for (int age = 0; age < size(); age++) {
      const float v = at(m, age);
      if (std::isnan(v) || v <= level) break;
      n++;
    }
    return n * 5;
  }

  // Changes once per finished 5-minute period, so the screen knows the charts moved
  uint32_t version() const { return version_; }

  // For the desktop preview: append a finished 5-minute average
  void push_for_preview(const Readings &r) { push_(r); }

 private:
  void open_bucket_(uint32_t id) {
    open_ = true;
    bucket_id_ = id;
    for (int m = 0; m < kMetrics; m++) {
      sum_[m] = 0;
      n_[m] = 0;
    }
  }
  void close_bucket_() {
    Readings avg;
    for (int m = 0; m < kMetrics; m++) avg.v[m] = n_[m] ? sum_[m] / n_[m] : NAN;
    push_(avg);
  }
  void push_(const Readings &r) {
    data_[head_] = r;
    head_ = (head_ + 1) % kSlots;
    if (size_ < kSlots) size_++;
    version_++;
  }

  Readings data_[kSlots];
  int head_ = 0, size_ = 0;
  bool open_ = false;
  uint32_t bucket_id_ = 0;
  float sum_[kMetrics] = {};
  int n_[kMetrics] = {};
  uint32_t version_ = 0;
};

}  // namespace air
