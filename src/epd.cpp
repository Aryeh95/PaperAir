#include "epd.h"

#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <lgfx/v1/platforms/esp32/Bus_EPD.h>
#include <lgfx/v1/platforms/esp32/Panel_EPD.hpp>

extern "C" {
#include <epdiy.h>
extern const EpdBoardDefinition paperdiy_board;   // epd_board_paperdiy.c
}

namespace epd {
namespace {

constexpr int kTemperatureC = 22;   // for the waveform's temperature range; the board has no sensor

EpdiyHighlevelState hl;
bool ready = false;
bool upside_down = false;
bool first = true;

// M5GFX has no way to hand its panel back, and keeps what it holds in protected and private
// members. These reach them without changing the library: an explicit template instantiation may
// name a private member, and a pointer to a protected one can be taken through a derived class.
template <typename Tag, typename Tag::type M>
struct Grant {
  friend typename Tag::type get(Tag) { return M; }
};
struct TaskTag {
  using type = TaskHandle_t lgfx::Panel_EPD::*;
  friend type get(TaskTag);
};
template struct Grant<TaskTag, &lgfx::Panel_EPD::_task_update_handle>;

struct BusAccess : lgfx::Bus_EPD {
  static esp_lcd_i80_bus_handle_t lgfx::Bus_EPD::*i80() { return &BusAccess::_i80_bus_handle; }
  static esp_lcd_panel_io_handle_t lgfx::Bus_EPD::*io() { return &BusAccess::_io_handle; }
};
struct PanelAccess : lgfx::Panel_EPD {
  static uint8_t lgfx::Panel_Device::*rotation() { return &PanelAccess::_internal_rotation; }
};

// Stops M5GFX's update task and frees the LCD peripheral it drives the panel through. M5.Display
// must not be drawn on after this.
bool release_m5gfx() {
  auto *panel = static_cast<lgfx::Panel_EPD *>(M5.Display.getPanel());
  if (!panel || M5.getBoard() != m5::board_t::board_M5PaperDIY) return false;
  M5.Display.waitDisplay();
  // Which way up the screen is: M5GFX's rotation, as set by setup_display(), in panel terms
  const uint8_t rot = panel->*PanelAccess::rotation();
  upside_down = (rot & 3) == 2;
  TaskHandle_t &task = panel->*get(TaskTag());
  if (task) {
    vTaskDelete(task);
    task = nullptr;
  }
  auto *bus = static_cast<lgfx::Bus_EPD *>(panel->getBus());
  bus->powerControl(false);
  esp_lcd_panel_io_handle_t &io = bus->*BusAccess::io();
  esp_lcd_i80_bus_handle_t &i80 = bus->*BusAccess::i80();
  if (io) esp_lcd_panel_io_del(io);
  if (i80) esp_lcd_del_i80_bus(i80);
  io = nullptr;
  i80 = nullptr;
  return true;
}

}  // namespace

bool begin() {
  if (!release_m5gfx()) {
    Serial.println("EPDiy: this isn't a PaperDIY, leaving the screen to M5GFX");
    return false;
  }
  static EpdDisplay_t panel = ED047TC1;
  panel.bus_speed = 16;   // MHz, as M5GFX drives this panel
  epd_init(&paperdiy_board, &panel, EPD_LUT_64K);
  hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
  ready = hl.front_fb != nullptr;
  Serial.printf("EPDiy: %s, %dx%d%s, %u bytes internal RAM free\n", ready ? "ready" : "no memory for the frame",
                epd_width(), epd_height(), upside_down ? " upside down" : "",
                (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  return ready;
}

void show(const uint8_t *grey, Update how) {
  if (!ready) return;
  const int w = epd_width(), h = epd_height();
  uint8_t *fb = epd_hl_get_framebuffer(&hl);
  // 4 bits a pixel, two to a byte, the left one in the low nibble
  for (int y = 0; y < h; y++) {
    const uint8_t *src = grey + (size_t) (upside_down ? h - 1 - y : y) * w;
    uint8_t *dst = fb + (size_t) y * w / 2;
    if (upside_down) {
      for (int x = 0; x < w; x += 2) dst[x / 2] = (src[w - 1 - x] >> 4) | (src[w - 2 - x] & 0xF0);
    } else {
      for (int x = 0; x < w; x += 2) dst[x / 2] = (src[x] >> 4) | (src[x + 1] & 0xF0);
    }
  }

  const unsigned long start = millis();
  epd_poweron();
  enum EpdDrawError err;
  if (how == Update::Clean || first) {
    // What's on the glass is unknown (first frame) or ghosted: flash it white, then draw from white.
    // epd_fullclear() blanks the front buffer, so the frame is kept and put back.
    static uint8_t *keep = nullptr;
    if (!keep) keep = static_cast<uint8_t *>(heap_caps_malloc((size_t) w * h / 2, MALLOC_CAP_SPIRAM));
    if (keep) memcpy(keep, fb, (size_t) w * h / 2);
    epd_fullclear(&hl, kTemperatureC);
    if (keep) memcpy(fb, keep, (size_t) w * h / 2);
    err = epd_hl_update_screen(&hl, MODE_EPDIY_WHITE_TO_GL16, kTemperatureC);
    first = false;
  } else {
    err = epd_hl_update_screen(&hl, MODE_GL16, kTemperatureC);
  }
  epd_poweroff();
  if (err != EPD_DRAW_SUCCESS) Serial.printf("EPDiy: update failed (0x%x)\n", (unsigned) err);
  Serial.printf("EPDiy: %s update in %lu ms\n", how == Update::Clean ? "clean" : "gentle", millis() - start);
}

}  // namespace epd
