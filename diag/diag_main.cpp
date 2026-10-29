// Display diagnostic for the PaperDIY: pio run -e diag -t upload, then watch the serial monitor.
//
// Logs what M5Unified found, the power chip's state, and then drives the e-paper through a simple
// black / white / bars cycle, timing each update. G3 and G4 are logged when pressed.
#include <M5Unified.h>

namespace {

const char *board_name(m5::board_t b) {
  switch (b) {
    case m5::board_t::board_M5PaperDIY: return "M5PaperDIY";
    case m5::board_t::board_M5PaperS3: return "M5PaperS3";
    case m5::board_t::board_unknown: return "unknown";
    default: return "other";
  }
}

void log_power() {
  auto &pm = M5.Power.M5pm1;
  Serial.printf("PM1: enabled=%d  PWR_CFG=0x%02x  EPD_PWR (GPIO2 latch)=%d  CHG_STAT (GPIO3 in)=%d  battery=%d%%  charging=%d\n",
                pm.isEnabled(), pm.readRegister8(0x06), pm.getGPIOOutputLatch(m5::M5PM1_Class::gpio2),
                pm.getGPIOInput(m5::M5PM1_Class::gpio3), M5.Power.getBatteryLevel(), (int) M5.Power.isCharging());
}

unsigned long timed(const char *what, void (*f)()) {
  const unsigned long t0 = millis();
  f();
  M5.Display.waitDisplay();
  const unsigned long dt = millis() - t0;
  Serial.printf("  %s: %lu ms\n", what, dt);
  return dt;
}

void black() { M5.Display.fillScreen(TFT_BLACK); }
void white() { M5.Display.fillScreen(TFT_WHITE); }
void bars() {
  const int w = M5.Display.width(), h = M5.Display.height();
  for (int i = 0; i < 16; ++i) M5.Display.fillRect(i * w / 16, 0, w / 16 + 1, h, M5.Display.color888(i * 17, i * 17, i * 17));
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextSize(3);
  M5.Display.drawString("PaperAir display test", 40, 40);
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.clear_display = false;
  M5.begin(cfg);
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n=== PaperAir display diagnostic ===");
  Serial.printf("board: %s (%d)  display: %dx%d  isEPD=%d  panel=%s\n", board_name(M5.getBoard()), (int) M5.getBoard(),
                M5.Display.width(), M5.Display.height(), (int) M5.Display.isEPD(), M5.Display.getPanel() ? "yes" : "none");
  log_power();

  Serial.println("Cycle 1, epd_fast:");
  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  timed("black", black);
  timed("white", white);
  timed("bars", bars);
  log_power();

  Serial.println("Cycle 2, epd_quality:");
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  timed("black", black);
  timed("white", white);
  timed("bars", bars);
  log_power();
  Serial.println("Done. Every 10 s from now: white, then bars. G3 / G4 presses are logged.");
}

void loop() {
  static unsigned long last = 0;
  static bool show_bars = false;
  M5.update();
  if (M5.BtnA.wasPressed()) Serial.println("G4 pressed");
  if (M5.BtnB.wasPressed()) Serial.println("G3 pressed");
  if (millis() - last > 10000) {
    last = millis();
    show_bars = !show_bars;
    timed(show_bars ? "bars" : "white", show_bars ? bars : white);
  }
  delay(20);
}
