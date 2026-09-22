#include "board_hal.h"

#if defined(DOGGY_WATCH_WAVESHARE_C6)

#include "time_offset.h"
#include "watch_sleep_policy.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <HWCDC.h>
#include <SensorPCF85063.hpp>
#include <SensorQMI8658.hpp>
#include <Wire.h>
#include <XPowersLib.h>
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
inline constexpr uint32_t kI2cFrequency = 400000;
inline constexpr uint8_t kImuAddress = 0x6B;
inline constexpr int kImuInt1 = 16;
inline constexpr uint8_t kImuWakeThresholdMg = 250;
inline constexpr int kWakePollMs = 50;
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
XPowersPMU power;
SensorPCF85063 rtc;
SensorQMI8658 imu;
bool board_ready = false;
bool shared_i2c_ready = false;
bool pmu_ready = false;
bool rtc_ready = false;
bool imu_ready = false;

}

// ================================================================================

static uint32_t watch_millis() {
    return millis();
}

// ================================================================================

static bool touch_read(uint16_t &x, uint16_t &y) {
    if (shared_i2c_ready == false) {
        return false;
    }
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
    shared_i2c_ready = Wire.begin(
            kTouchSda,
            kTouchScl,
            kI2cFrequency);
    if (shared_i2c_ready == false) {
        Serial.println("I2C initialization failed");
    }

    pmu_ready = power.begin(
            Wire,
            AXP2101_SLAVE_ADDRESS,
            kTouchSda,
            kTouchScl);
    if (pmu_ready) {
        // Measurement only — do not retune DCDC/LDO maps; the panel already
        // runs on the vendor default rails.
        power.enableBattDetection();
        power.enableBattVoltageMeasure();
        power.enableSystemVoltageMeasure();
    } else {
        Serial.println("AXP2101 initialization failed");
    }

    rtc_ready = rtc.begin(Wire, kTouchSda, kTouchScl);
    if (rtc_ready == false) {
        Serial.println("PCF85063 initialization failed");
    }

    imu_ready = imu.begin(Wire, kImuAddress, kTouchSda, kTouchScl);
    if (imu_ready) {
        pinMode(kImuInt1, INPUT_PULLUP);
        // 250 mg is a high WoM bar so a desk bump is less likely to wake the
        // watch; walking still can, unlike the S3 tilt gesture.
        imu_ready = imu.configWakeOnMotion(
                kImuWakeThresholdMg,
                SensorQMI8658::ACC_ODR_LOWPOWER_128Hz,
                SensorQMI8658::INTERRUPT_PIN_1,
                1) == 0;
    }
    if (imu_ready == false) {
        Serial.println("QMI8658 initialization failed");
    }

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

bool watch_board_battery(int &percent, bool &charging) {
    if (shared_i2c_ready == false
            || pmu_ready == false
            || power.isBatteryConnect() == false) {
        return false;
    }
    const int reading = power.getBatteryPercent();
    if (reading < 0) {
        return false;
    }
    percent = reading;
    charging = power.isCharging();
    return true;
}

// ================================================================================

int watch_board_safe_inset() {
    return kSafeInset;
}

// ================================================================================

bool watch_board_has_rtc() {
    return shared_i2c_ready && rtc_ready;
}

// ================================================================================

bool watch_board_rtc_read(std::time_t &utc) {
    if (shared_i2c_ready == false
            || rtc_ready == false
            || rtc.isClockIntegrityGuaranteed() == false) {
        return false;
    }
    const std::tm stored = rtc.getDateTime().toUnixTime();
    const std::time_t stamp = watch_utc_time_from_civil(stored);
    if (stamp < kWatchMinimumValidTime) {
        return false;
    }
    utc = stamp;
    return true;
}

// ================================================================================

bool watch_board_rtc_write(std::time_t utc) {
    if (shared_i2c_ready == false
            || rtc_ready == false
            || utc < kWatchMinimumValidTime) {
        return false;
    }
    std::tm broken_down{};
    gmtime_r(&utc, &broken_down);
    rtc.setDateTime(RTC_DateTime(broken_down));
    return true;
}

// ================================================================================

// Polls the same two wake lines light sleep would have armed. Returns false if
// the host leaves the bus first, so the caller can fall back to real sleep
// instead of burning battery in this loop.
static bool wait_for_wake_line() {
    const int imu_level = imu_ready ? digitalRead(kImuInt1) : HIGH;
    while (HWCDC::isPlugged()) {
        if (digitalRead(kTouchInterrupt) == LOW) {
            return true;
        }
        if (imu_ready && digitalRead(kImuInt1) != imu_level) {
            return true;
        }
        delay(kWakePollMs);
    }
    return false;
}

// ================================================================================

// Touch pulls INT low on contact, so that line has a real polarity. The QMI8658
// wake-on-motion line does not: it toggles on every event, which is why
// SensorLib's own example watches it for CHANGE rather than for a level.
void watch_board_sleep() {
    // Reading STATUS1 clears the motion event behind the last toggle. The pin
    // level is whatever that toggle left, which is why it is sampled below
    // rather than assumed.
    if (imu_ready) {
        imu.getStatusRegister();
    }
    if (watch_active_low_wake_armable(digitalRead(kTouchInterrupt) == HIGH) == false
            && imu_ready == false) {
        // Sleeping with no wake source would blank a watch nothing can bring
        // back, and sleeping with an already-asserted line just flashes the
        // panel. Stay awake and try again on the next idle deadline.
        return;
    }

    if (board_ready) {
        graphics->displayOff();
    }

    bool woke = false;
    if (watch_sleep_uses_light_sleep(HWCDC::isPlugged()) == false) {
        woke = wait_for_wake_line();
    }
    if (woke == false) {
        const bool touch_armable = watch_active_low_wake_armable(
                digitalRead(kTouchInterrupt) == HIGH);
        if (touch_armable) {
            gpio_wakeup_enable(
                    static_cast<gpio_num_t>(kTouchInterrupt),
                    GPIO_INTR_LOW_LEVEL);
        }
        if (imu_ready) {
            const WatchWakeLevel level = watch_toggling_wake_level(
                    digitalRead(kImuInt1) == HIGH);
            gpio_wakeup_enable(
                    static_cast<gpio_num_t>(kImuInt1),
                    level == kWatchWakeLevelHigh
                            ? GPIO_INTR_HIGH_LEVEL
                            : GPIO_INTR_LOW_LEVEL);
        }
        esp_sleep_enable_gpio_wakeup();

        // Arduino-ESP32 documents end()/begin() as the supported way to release
        // and restore Wire. Cycling it avoids retaining an IDF device handle
        // that the C6 can reject after light sleep with ESP_ERR_INVALID_STATE.
        if (Wire.end() == false) {
            Serial.println("I2C shutdown before sleep failed");
        }
        shared_i2c_ready = false;
        esp_light_sleep_start();
        shared_i2c_ready = Wire.begin(
                kTouchSda,
                kTouchScl,
                kI2cFrequency);
        if (shared_i2c_ready == false) {
            Serial.println("I2C recovery after wake failed");
        }

        // Names the pin that ended the sleep. A zero mask means something other
        // than these two lines woke the watch.
        Serial.printf(
                "watch: wake gpio mask 0x%llx\n",
                static_cast<unsigned long long>(
                        esp_sleep_get_gpio_wakeup_status()));

        if (touch_armable) {
            gpio_wakeup_disable(static_cast<gpio_num_t>(kTouchInterrupt));
        }
        if (imu_ready) {
            gpio_wakeup_disable(static_cast<gpio_num_t>(kImuInt1));
        }
    }
    if (shared_i2c_ready && imu_ready) {
        imu.getStatusRegister();
    }
    if (board_ready) {
        graphics->displayOn();
    }
}

#endif
