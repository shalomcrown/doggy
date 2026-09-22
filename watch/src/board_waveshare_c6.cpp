#include "board_hal.h"

#if defined(DOGGY_WATCH_WAVESHARE_C6)

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <lvgl.h>

#include <algorithm>
#include <cstdlib>

namespace {

inline constexpr int kLcdSclk = 0;
inline constexpr int kLcdData0 = 1;
inline constexpr int kLcdData1 = 2;
inline constexpr int kLcdData2 = 3;
inline constexpr int kLcdData3 = 4;
inline constexpr int kLcdCs = 5;
inline constexpr int kLcdReset = 11;
inline constexpr int kLcdWidth = 410;
inline constexpr int kLcdHeight = 502;
inline constexpr int kTouchSda = 8;
inline constexpr int kTouchScl = 7;
inline constexpr int kTouchInterrupt = 15;
inline constexpr int kTouchReset = 10;
inline constexpr uint8_t kTouchAddress = 0x38;
inline constexpr uint8_t kTouchPointsRegister = 0x02;
inline constexpr std::size_t kDrawBufferLines = 20;
// The panel is a rounded rectangle, so a corner of radius R hides everything
// closer to the edge than R * (1 - 1 / sqrt(2)). 48 clears R up to about 160.
inline constexpr int kSafeInset = 48;

Arduino_DataBus *display_bus = new Arduino_ESP32QSPI(
        kLcdCs,
        kLcdSclk,
        kLcdData0,
        kLcdData1,
        kLcdData2,
        kLcdData3);
Arduino_GFX *graphics = new Arduino_CO5300(
        display_bus,
        kLcdReset,
        0,
        kLcdWidth,
        kLcdHeight,
        22,
        0,
        0,
        0);
lv_display_t *display = nullptr;
uint8_t *draw_buffer = nullptr;
bool board_ready = false;

}

// ================================================================================

static uint32_t watch_millis() {
    return millis();
}

// ================================================================================

static bool touch_read(uint16_t &x, uint16_t &y) {
    Wire.beginTransmission(kTouchAddress);
    Wire.write(kTouchPointsRegister);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom(kTouchAddress, static_cast<uint8_t>(5)) != 5) {
        return false;
    }
    const uint8_t points = Wire.read();
    const uint8_t x_high = Wire.read();
    const uint8_t x_low = Wire.read();
    const uint8_t y_high = Wire.read();
    const uint8_t y_low = Wire.read();
    if ((points & 0x0F) == 0) {
        return false;
    }
    x = static_cast<uint16_t>(((x_high & 0x0F) << 8) | x_low);
    y = static_cast<uint16_t>(((y_high & 0x0F) << 8) | y_low);
    return x < kLcdWidth && y < kLcdHeight;
}

// ================================================================================

static void display_flush(
        lv_display_t *lv_display,
        const lv_area_t *area,
        uint8_t *pixels) {
    graphics->draw16bitRGBBitmap(
            area->x1,
            area->y1,
            reinterpret_cast<uint16_t *>(pixels),
            lv_area_get_width(area),
            lv_area_get_height(area));
    lv_display_flush_ready(lv_display);
}

// ================================================================================

static void display_rounder(lv_event_t *event) {
    lv_area_t *area = static_cast<lv_area_t *>(lv_event_get_param(event));
    if (area == nullptr) {
        return;
    }
    area->x1 = 0;
    area->x2 = kLcdWidth - 1;
    area->y1 &= ~1;
    area->y2 |= 1;
    area->y1 = std::max<int32_t>(0, area->y1);
    area->y2 = std::min<int32_t>(kLcdHeight - 1, area->y2);
}

// ================================================================================

static void touchpad_read(lv_indev_t *, lv_indev_data_t *data) {
    uint16_t x = 0;
    uint16_t y = 0;
    if (touch_read(x, y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ================================================================================

bool watch_board_begin() {
    Serial.begin(115200);
    pinMode(kTouchReset, OUTPUT);
    digitalWrite(kTouchReset, LOW);
    delay(10);
    digitalWrite(kTouchReset, HIGH);
    delay(200);
    pinMode(kTouchInterrupt, INPUT_PULLUP);
    Wire.begin(kTouchSda, kTouchScl);
    Wire.setClock(400000);

    if (graphics == nullptr || graphics->begin() == false) {
        Serial.println("Display initialization failed");
        return false;
    }
    graphics->fillScreen(RGB565_BLACK);

    lv_init();
    lv_tick_set_cb(watch_millis);
    const std::size_t draw_buffer_bytes =
            kLcdWidth * kDrawBufferLines
            * lv_color_format_get_size(LV_COLOR_FORMAT_RGB565);
    draw_buffer = static_cast<uint8_t *>(std::malloc(draw_buffer_bytes));
    if (draw_buffer == nullptr) {
        Serial.println("LVGL draw-buffer allocation failed");
        return false;
    }

    display = lv_display_create(kLcdWidth, kLcdHeight);
    if (display == nullptr) {
        Serial.println("LVGL display creation failed");
        return false;
    }
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, display_flush);
    lv_display_set_buffers(
            display,
            draw_buffer,
            nullptr,
            draw_buffer_bytes,
            LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(
            display,
            display_rounder,
            LV_EVENT_INVALIDATE_AREA,
            nullptr);

    lv_indev_t *touch = lv_indev_create();
    if (touch == nullptr) {
        Serial.println("LVGL touch creation failed");
        return false;
    }
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touchpad_read);
    lv_indev_set_display(touch, display);
    board_ready = true;
    return true;
}

// ================================================================================

void watch_board_service() {
    if (board_ready) {
        lv_timer_handler();
    }
    delay(5);
}

// ================================================================================

// TODO: this board's battery sits behind an undocumented gauge; the UI shows the
// unknown symbol until the charger/ADC path is confirmed against the schematic.
bool watch_board_battery(int &, bool &) {
    return false;
}

// ================================================================================

int watch_board_safe_inset() {
    return kSafeInset;
}

// ================================================================================

// This board has no battery-backed calendar chip, so the time comes from NTP on
// every boot.
bool watch_board_has_rtc() {
    return false;
}

// ================================================================================

bool watch_board_rtc_read(std::time_t &) {
    return false;
}

// ================================================================================

bool watch_board_rtc_write(std::time_t) {
    return false;
}

// ================================================================================

// The touch controller pulls its interrupt line low on contact, and the board
// carries no motion sensor, so touch is the only way back out of sleep.
void watch_board_sleep() {
    if (board_ready) {
        graphics->displayOff();
    }
    gpio_wakeup_enable(
            static_cast<gpio_num_t>(kTouchInterrupt),
            GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    esp_light_sleep_start();

    gpio_wakeup_disable(static_cast<gpio_num_t>(kTouchInterrupt));
    if (board_ready) {
        graphics->displayOn();
    }
}

#endif
