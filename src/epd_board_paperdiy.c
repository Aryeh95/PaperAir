// EPDiy board definition for the M5Stack PaperDIY (the same panel wiring as the PaperS3).
//
// The panel's rails come from a boost converter that GPIO46 switches on, with VCOM fixed in
// hardware; the M5PM1's GPIO2 feeds it and is turned on by M5GFX when it detects the board. There
// is no TPS65185 and no mode pin, so power-up is the same three lines M5GFX's Bus_EPD drives:
// OE, then PWR, then (via the LCD driver) SPV.
#include <driver/gpio.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "epd_board.h"
#include "epdiy.h"
#include "output_lcd/lcd_driver.h"

#define PIN_OE GPIO_NUM_45
#define PIN_PWR GPIO_NUM_46

static lcd_bus_config_t lcd_config = {
    .data = {GPIO_NUM_6, GPIO_NUM_14, GPIO_NUM_7, GPIO_NUM_12, GPIO_NUM_9, GPIO_NUM_11, GPIO_NUM_8, GPIO_NUM_10},
    .clock = GPIO_NUM_16,        // CL
    .ckv = GPIO_NUM_18,          // CKV
    .start_pulse = GPIO_NUM_13,  // SPH
    .leh = GPIO_NUM_15,          // LE
    .stv = GPIO_NUM_17,          // SPV
};

static void out_low(gpio_num_t pin) {
    gpio_hold_dis(pin);
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
}

static void board_init(uint32_t epd_row_width, const EpdInitConfig* init_config) {
    (void)epd_row_width;
    (void)init_config;
    // M5GFX had these routed to its own LCD bus (GPIO46 as a dummy DC line, SPH as chip select),
    // and some data lanes come up open-drain: back to plain outputs, low, before EPDiy takes them
    out_low(PIN_OE);
    out_low(PIN_PWR);
    for (int i = 0; i < 8; i++) out_low(lcd_config.data[i]);
    out_low(lcd_config.clock);
    out_low(lcd_config.ckv);
    out_low(lcd_config.start_pulse);
    out_low(lcd_config.leh);
    out_low(lcd_config.stv);

    const EpdDisplay_t* display = epd_get_display();
    LcdEpdConfig_t config = {
        .pixel_clock = display->bus_speed * 1000 * 1000,
        .ckv_high_time = 60,
        .line_front_porch = 4,
        .le_high_time = 4,
        .bus_width = display->bus_width,
        .bus = lcd_config,
    };
    epd_lcd_init(&config, display->width, display->height);
}

static void board_deinit(void) {
    epd_lcd_deinit();
    gpio_set_level(PIN_PWR, 0);
    gpio_set_level(PIN_OE, 0);
}

static void board_set_ctrl(epd_ctrl_state_t* state, const epd_ctrl_state_t* const mask) {
    if (mask->ep_output_enable) gpio_set_level(PIN_OE, state->ep_output_enable ? 1 : 0);
}

static void board_poweron(epd_ctrl_state_t* state) {
    const epd_ctrl_state_t mask = {.ep_output_enable = true};
    state->ep_output_enable = true;
    board_set_ctrl(state, &mask);
    esp_rom_delay_us(100);
    gpio_set_level(PIN_PWR, 1);
    vTaskDelay(pdMS_TO_TICKS(2));   // let the rails come up
    state->ep_stv = true;
    state->ep_sth = true;
}

static void board_poweroff(epd_ctrl_state_t* state) {
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(PIN_PWR, 0);
    esp_rom_delay_us(10);
    const epd_ctrl_state_t mask = {.ep_output_enable = true};
    state->ep_output_enable = false;
    board_set_ctrl(state, &mask);
    state->ep_stv = false;
}

static float board_temperature(void) { return 22; }

const EpdBoardDefinition paperdiy_board = {
    .init = board_init,
    .deinit = board_deinit,
    .set_ctrl = board_set_ctrl,
    .poweron = board_poweron,
    .measure_vcom = NULL,
    .poweroff = board_poweroff,
    .set_vcom = NULL,
    .get_temperature = board_temperature,
    .gpio_set_direction = NULL,
    .gpio_read = NULL,
    .gpio_write = NULL,
};
