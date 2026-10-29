// Unit tests for the air library. Run on a PC with: pio test -e native
#include <unity.h>

#include <cstring>
#include <string>

#include "advice.h"
#include "airnow.h"
#include "aqi.h"
#include "humidity.h"
#include "purpleair.h"
#include "readings.h"
#include "sensirion.h"

using namespace air;

void setUp() {}
void tearDown() {}

// ---- US AQI ----------------------------------------------------------------
static void test_aqi_breakpoints() {
  TEST_ASSERT_EQUAL(0, aqi_from_pm25(0));
  TEST_ASSERT_EQUAL(50, aqi_from_pm25(9.0f));
  TEST_ASSERT_EQUAL(50, aqi_from_pm25(9.05f));  // truncated to 9.0
  TEST_ASSERT_EQUAL(51, aqi_from_pm25(9.1f));
  TEST_ASSERT_EQUAL(100, aqi_from_pm25(35.4f));
  TEST_ASSERT_EQUAL(101, aqi_from_pm25(35.5f));
  TEST_ASSERT_EQUAL(150, aqi_from_pm25(55.4f));
  TEST_ASSERT_EQUAL(200, aqi_from_pm25(125.4f));
  TEST_ASSERT_EQUAL(500, aqi_from_pm25(325.4f));
  TEST_ASSERT_EQUAL(500, aqi_from_pm25(900));
  TEST_ASSERT_EQUAL(-1, aqi_from_pm25(NAN));
}

static void test_aqi_round_trip() {
  for (int a = 0; a <= 500; a++) {
    const int b = aqi_from_pm25(pm25_from_aqi(a) + 0.001f);
    TEST_ASSERT_INT_WITHIN(1, a, b);
  }
  TEST_ASSERT_TRUE(std::isnan(pm25_from_aqi(-1)));
  TEST_ASSERT_EQUAL_STRING("clean", aqi_word(26));
  TEST_ASSERT_EQUAL_STRING("smoky", aqi_word(160));
}

// ---- Humidity --------------------------------------------------------------
static void test_humidity_compensation() {
  TEST_ASSERT_EQUAL_FLOAT(40, compensate_humidity(40, 25, 0));
  const float rh = compensate_humidity(40, 25, -2);  // sensor 2 C warm: real RH is higher
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 45.1f, rh);
  TEST_ASSERT_EQUAL_FLOAT(100, compensate_humidity(99, 25, -10));
}

// ---- PurpleAir -------------------------------------------------------------
static void test_epa_correction() {
  for (float x : {30.0f, 50.0f, 210.0f, 260.0f}) {  // continuous where the formula changes
    TEST_ASSERT_FLOAT_WITHIN(0.05f, epa_correct_purpleair(x - 0.001f, 50), epa_correct_purpleair(x + 0.001f, 50));
  }
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.68f, epa_correct_purpleair(10, 50));  // 5.24 - 4.31 + 5.75
  TEST_ASSERT_EQUAL_FLOAT(0, epa_correct_purpleair(1, 90));
  TEST_ASSERT_TRUE(std::isnan(epa_correct_purpleair(10, NAN)));
}

static void test_channel_agreement() {
  TEST_ASSERT_TRUE(purpleair_channels_agree(3, 7.9f));   // under 5 apart
  TEST_ASSERT_FALSE(purpleair_channels_agree(2, 20));    // far apart
  TEST_ASSERT_TRUE(purpleair_channels_agree(50, 60));    // 10 apart but only 18%
  TEST_ASSERT_FALSE(purpleair_channels_agree(NAN, 3));
}

static PurpleAirReading pa(const char *json) { return parse_purpleair(json, strlen(json)); }

static void test_parse_purpleair() {
  auto r = pa(R"({"data_time_stamp":1000,"sensor":{"name":"Test St","location_type":0,"last_seen":990,
      "humidity":40,"pm2.5_cf_1":10.2,"pm2.5_cf_1_a":10.0,"pm2.5_cf_1_b":10.4}})");
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_STRING("Test St", r.name.c_str());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.2f, r.raw_cf1);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, epa_correct_purpleair(10.2f, 40), r.pm25);

  r = pa(R"({"sensor":{"name":"In","location_type":1,"pm2.5_cf_1":3}})");
  TEST_ASSERT_FALSE(r.ok);  // indoor sensor

  r = pa(R"({"data_time_stamp":10000,"sensor":{"location_type":0,"last_seen":1000,"pm2.5_cf_1":3,"humidity":40}})");
  TEST_ASSERT_FALSE(r.ok);  // stale

  r = pa(R"({"sensor":{"location_type":0,"pm2.5_cf_1_a":2,"pm2.5_cf_1_b":20,"humidity":40}})");
  TEST_ASSERT_FALSE(r.ok);  // channels disagree

  r = pa(R"({"sensor":{"location_type":0,"pm2.5_cf_1":8}})");
  TEST_ASSERT_TRUE(r.ok);  // no humidity: assumed 50%
  TEST_ASSERT_TRUE(r.humidity_assumed);

  r = pa(R"({"error":"InvalidApiKeyError","description":"Invalid API key."})");
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("Invalid API key.", r.error.c_str());
}

// ---- AirNow ----------------------------------------------------------------
static AirNowReading an(const char *json) { return parse_airnow(json, strlen(json)); }

static void test_parse_airnow() {
  // Current service (since October 2026)
  auto r = an(R"([{"parameterName":"PM2.5","nowcastAQI":14,"aqiCategoryName":"Good"},
                  {"parameterName":"OZONE","nowcastAQI":42,"aqiCategoryName":"Good"}])");
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL(42, r.aqi);
  TEST_ASSERT_EQUAL(14, r.pm25_aqi);
  // Retired service's field names still work
  r = an(R"([{"ParameterName":"PM2.5","AQI":51}])");
  TEST_ASSERT_EQUAL(51, r.pm25_aqi);
  // Errors come back as an object
  r = an(R"({"WebServiceError":[{"Message":"No observations were found."}]})");
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("No observations were found.", r.error.c_str());
}

// ---- History ---------------------------------------------------------------
static void test_history() {
  History h;
  Readings r;
  r[Metric::CO2] = 900;
  h.add(0, r);
  r[Metric::CO2] = 1100;
  h.add(60 * 1000, r);
  TEST_ASSERT_EQUAL(1, h.size());
  TEST_ASSERT_EQUAL_FLOAT(1000, h.at(Metric::CO2, 0));  // average of the open 5 minutes
  h.add(History::kBucketMs * 3, r);                      // two empty periods in between
  TEST_ASSERT_EQUAL(4, h.size());
  TEST_ASSERT_TRUE(std::isnan(h.at(Metric::CO2, 1)));
  TEST_ASSERT_EQUAL_FLOAT(1000, h.at(Metric::CO2, 3));
}

static void test_history_save_restore() {
  History h;
  Readings r;
  for (int i = 0; i < 10; i++) {
    r[Metric::CO2] = 500 + i * 10;
    h.push_for_preview(r);
  }
  r[Metric::CO2] = 777;
  h.add(1000, r);  // an open period too
  static uint8_t buf[History::kSaveSize];
  const uint32_t t = 1700000000;
  const size_t n = h.save(buf, sizeof buf, t);
  TEST_ASSERT_EQUAL(16 + 11 * kMetrics * sizeof(float), n);

  // Back after 20 minutes off, with the sensor already reading for 30 s before the clock was set
  History g;
  r[Metric::CO2] = 888;
  g.add(30 * 1000, r);
  TEST_ASSERT_TRUE(g.restore(buf, n, t + 20 * 60 + 30, 30 * 1000));
  TEST_ASSERT_EQUAL(1 + 4 + 11, g.size());                // open, 4 empty, 11 saved
  TEST_ASSERT_EQUAL_FLOAT(888, g.at(Metric::CO2, 0));
  TEST_ASSERT_TRUE(std::isnan(g.at(Metric::CO2, 1)));
  TEST_ASSERT_TRUE(std::isnan(g.at(Metric::CO2, 4)));
  TEST_ASSERT_EQUAL_FLOAT(777, g.at(Metric::CO2, 5));     // the period that was open when saved
  TEST_ASSERT_EQUAL_FLOAT(590, g.at(Metric::CO2, 6));
  TEST_ASSERT_EQUAL_FLOAT(500, g.at(Metric::CO2, 15));

  // Too old, bad data, or a history that already has finished periods: nothing changes
  History x;
  TEST_ASSERT_FALSE(x.restore(buf, n, t + 25 * 3600, 0));
  buf[0] ^= 1;
  TEST_ASSERT_FALSE(x.restore(buf, n, t + 60, 0));
  buf[0] ^= 1;
  TEST_ASSERT_FALSE(g.restore(buf, n, t + 60, 0));
  TEST_ASSERT_EQUAL(0, x.size());
}

static void test_minutes_above() {
  History h;
  Readings r;
  for (int i = 0; i < 40; i++) {  // 40 periods: 10 at 600, then 30 at 1,100 ppm
    r[Metric::CO2] = i < 10 ? 600 : 1100;
    h.push_for_preview(r);
  }
  TEST_ASSERT_EQUAL(150, h.minutes_above(Metric::CO2, 800));
  TEST_ASSERT_EQUAL(0, h.minutes_above(Metric::CO2, 1200));
}

// ---- Advice ----------------------------------------------------------------
static Readings room(float co2, float pm25, float rh = 45, float voc = 100, float nox = 1, float hcho = NAN) {
  Readings r;
  r[Metric::CO2] = co2;
  r[Metric::PM25] = pm25;
  r[Metric::Humidity] = rh;
  r[Metric::VOC] = voc;
  r[Metric::NOx] = nox;
  r[Metric::HCHO] = hcho;
  r[Metric::Temp] = 22;
  return r;
}
static Outdoor outside(int aqi) {
  Outdoor o;
  o.ok = true;
  o.aqi = aqi;
  o.pm25 = pm25_from_aqi(aqi);
  return o;
}

static void test_advice_states() {
  History h;
  auto a = advise(Readings(), h, Outdoor(), false, false);
  TEST_ASSERT_EQUAL(Mood::Starting, a.mood);
  a = advise(Readings(), h, Outdoor(), true, false);
  TEST_ASSERT_EQUAL(Mood::NoSensor, a.mood);

  a = advise(room(520, 3), h, outside(26), true, true);
  TEST_ASSERT_EQUAL(Mood::Fresh, a.mood);
  TEST_ASSERT_EQUAL_STRING("The air is fresh.", a.headline.c_str());
  TEST_ASSERT_EQUAL_STRING("Outside is clean too.", a.outdoor.c_str());

  a = advise(room(1180, 5), h, outside(26), true, true);
  TEST_ASSERT_EQUAL(Mood::Act, a.mood);
  TEST_ASSERT_EQUAL(Topic::CO2, a.topic);
  TEST_ASSERT_EQUAL_STRING("Open a window.", a.headline.c_str());
  TEST_ASSERT_EQUAL_STRING("CO₂ is at 1,180 ppm.", a.detail.c_str());
  TEST_ASSERT_EQUAL_STRING("Outside is clean (AQI 26), so it's a good time to air out.", a.outdoor.c_str());

  a = advise(room(1600, 5), h, outside(160), true, true);  // urgent, but smoky outside
  TEST_ASSERT_EQUAL(Mood::Urgent, a.mood);
  TEST_ASSERT_EQUAL_STRING("Air out briefly.", a.headline.c_str());

  a = advise(room(600, 22), h, outside(26), true, true);  // particles, clean outside
  TEST_ASSERT_EQUAL(Topic::Particles, a.topic);
  TEST_ASSERT_EQUAL_STRING("Run the air purifier.", a.headline.c_str());
  TEST_ASSERT_EQUAL_STRING("Fine particles are up (22 µg/m³).", a.detail.c_str());
  TEST_ASSERT_EQUAL_STRING("Outside is cleaner (AQI 26), so opening a window helps too.", a.outdoor.c_str());

  a = advise(room(600, 5), h, outside(170), true, true);  // fine inside, smoke outside
  TEST_ASSERT_EQUAL(Topic::OutdoorSmoke, a.topic);
  TEST_ASSERT_EQUAL_STRING("Keep the windows shut.", a.headline.c_str());
  TEST_ASSERT_TRUE(a.outdoor.empty());

  a = advise(room(600, 5, 45, 100, 1, 95), h, outside(26), true, true);
  TEST_ASSERT_EQUAL(Topic::HCHO, a.topic);
  a = advise(room(600, 5, 22), h, Outdoor(), true, true);
  TEST_ASSERT_EQUAL(Topic::Dry, a.topic);
  a = advise(room(600, 5, 45, 100, 64), h, Outdoor(), true, true);
  TEST_ASSERT_EQUAL(Topic::NOx, a.topic);
}

static void test_advice_duration() {
  History h;
  Readings r = room(1180, 5);
  for (int i = 0; i < 36; i++) h.push_for_preview(r);  // 3 hours above 800 ppm
  auto a = advise(r, h, Outdoor(), true, true);
  TEST_ASSERT_EQUAL_STRING("CO₂ has been building for about 3 hours.", a.detail.c_str());
}

static void test_formatting() {
  TEST_ASSERT_EQUAL_STRING("1,180", with_commas(1180).c_str());
  TEST_ASSERT_EQUAL_STRING("980", with_commas(980).c_str());
  TEST_ASSERT_EQUAL_STRING("12,345", with_commas(12345).c_str());
  TEST_ASSERT_EQUAL_STRING("40 minutes", duration_words(40).c_str());
  TEST_ASSERT_EQUAL_STRING("about 1 hour", duration_words(70).c_str());
}

// ---- Sensirion protocol -------------------------------------------------------
static void test_sensirion_crc() {
  const uint8_t beef[] = {0xBE, 0xEF};  // the datasheets' worked example
  TEST_ASSERT_EQUAL_HEX8(0x92, sensirion_crc(beef, 2));
  uint8_t bytes[6];
  const uint16_t words[] = {0xBEEF, 0x0123};
  sensirion_pack(words, 2, bytes);
  uint16_t back[2];
  TEST_ASSERT_TRUE(sensirion_unpack(bytes, 2, back));
  TEST_ASSERT_EQUAL_HEX16(0x0123, back[1]);
  bytes[4] ^= 1;  // corrupt the second word
  TEST_ASSERT_FALSE(sensirion_unpack(bytes, 2, back));
}

static void test_decode_sen66() {
  const uint16_t w[9] = {32, 58, 70, 94, 4123, (uint16_t) 4360, 1120, 10, 1180};
  Sen66Values s = decode_sen66(w);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.8f, s.pm25);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 41.23f, s.humidity);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.8f, s.temperature);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 112, s.voc);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1180, s.co2);
  const uint16_t neg[9] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x7FFF, (uint16_t) -400, 0x7FFF, 0x7FFF, 0xFFFF};
  s = decode_sen66(neg);
  TEST_ASSERT_TRUE(std::isnan(s.pm25));
  TEST_ASSERT_TRUE(std::isnan(s.co2));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -2, s.temperature);
}

static void test_decode_sfa40() {
  const uint16_t ready[4] = {235, 32768, 26214, 0x0000};
  Sfa40Values s = decode_sfa40(ready);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 23.5f, s.hcho);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 56.5f, s.humidity);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f, s.temperature);
  const uint16_t warming[4] = {235, 32768, 26214, 0x0100};
  s = decode_sfa40(warming);
  TEST_ASSERT_TRUE(s.warming_up);
  TEST_ASSERT_TRUE(std::isnan(s.hcho));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_aqi_breakpoints);
  RUN_TEST(test_aqi_round_trip);
  RUN_TEST(test_humidity_compensation);
  RUN_TEST(test_epa_correction);
  RUN_TEST(test_channel_agreement);
  RUN_TEST(test_parse_purpleair);
  RUN_TEST(test_parse_airnow);
  RUN_TEST(test_history);
  RUN_TEST(test_history_save_restore);
  RUN_TEST(test_minutes_above);
  RUN_TEST(test_advice_states);
  RUN_TEST(test_advice_duration);
  RUN_TEST(test_formatting);
  RUN_TEST(test_sensirion_crc);
  RUN_TEST(test_decode_sen66);
  RUN_TEST(test_decode_sfa40);
  return UNITY_END();
}
