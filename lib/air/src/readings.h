#pragma once
// One set of indoor readings, and a 24-hour history of them.
#include <cmath>
#include <cstddef>
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
// The device saves it to flash now and then (save / restore), so it survives a restart.
class History {
 public:
  static constexpr uint32_t kBucketMs = 5UL * 60UL * 1000UL;
  static constexpr int kSlots = 288;
  static constexpr size_t kSaveSize = 16 + (size_t) kSlots * kMetrics * sizeof(float);

  // Writes the finished periods, and the current one as if finished, newest first, with the
  // wall-clock time (Unix seconds). Returns the bytes used.
  size_t save(uint8_t *out, size_t cap, uint32_t now_unix) const {
    const int n = size();
    if (cap < 16 + (size_t) n * kMetrics * sizeof(float)) return 0;
    uint32_t head[4] = {kMagic, (uint32_t) n, now_unix, 0};
    copy_(out, head, 16);
    uint8_t *p = out + 16;
    for (int age = 0; age < n; age++) {
      for (int m = 0; m < kMetrics; m++) {
        const float v = at((Metric) m, age);
        copy_(p, &v, sizeof v);
        p += sizeof v;
      }
    }
    return p - out;
  }

  // Loads what save() wrote, before the first reading has finished a period: the saved periods go
  // in first, then empty ones for the time the device was off. `now_unix` is the wall-clock time
  // now and `uptime_ms` how long since boot, so readings taken before the clock was set keep
  // their place. Returns false, and changes nothing, if the data is unusable or over a day old.
  bool restore(const uint8_t *in, size_t len, uint32_t now_unix, uint32_t uptime_ms) {
    if (size_ != 0 || len < 16) return false;
    uint32_t head[4];
    copy_(head, in, 16);
    const uint32_t n = head[1], saved_at = head[2];
    if (head[0] != kMagic || n > (uint32_t) kSlots || len < 16 + n * kMetrics * sizeof(float)) return false;
    const uint32_t boot_unix = now_unix - uptime_ms / 1000;
    if (boot_unix < saved_at) return false;
    const uint32_t gap = (boot_unix - saved_at) / (kBucketMs / 1000);
    if (gap >= (uint32_t) kSlots) return false;
    const uint8_t *p = in + 16;
    for (int age = (int) n - 1; age >= 0; age--) {  // oldest first
      Readings r;
      copy_(r.v, p + (size_t) age * kMetrics * sizeof(float), kMetrics * sizeof(float));
      push_(r);
    }
    for (uint32_t i = 0; i < gap; i++) push_(Readings());
    return true;
  }

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

  static constexpr uint32_t kMagic = 0x50414831;  // "PAH1"
  static void copy_(void *dst, const void *src, size_t n) {
    auto *d = (uint8_t *) dst;
    auto *s = (const uint8_t *) src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
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
