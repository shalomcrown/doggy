#include "board_hal.h"

#if defined(DOGGY_WATCH_TWATCH_S3)

#include "time_offset.h"

#include <Arduino.h>
#include <LV_Helper.h>
#include <LilyGoLib.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <lvgl.h>

namespace {

// LilyGoLib arms these two pins with edge interrupts during begin(). Light sleep
// needs level triggers instead, so the pins are put back afterwards.
inline constexpr gpio_int_type_t kTouchAwakeTrigger = GPIO_INTR_NEGEDGE;
inline constexpr gpio_int_type_t kSensorAwakeTrigger = GPIO_INTR_POSEDGE;

}

// ================================================================================

static bool device_online(uint32_t bit) {
    return (instance.getDeviceProbe() & bit) != 0;
}

// ================================================================================

bool watch_board_begin() {
    Serial.begin(115200);
    instance.begin();
    beginLvglHelper(instance);
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
    return true;
}

// ================================================================================

void watch_board_service() {
    lv_timer_handler();
    delay(5);
}

// ================================================================================

bool watch_board_battery(int &percent, bool &charging) {
    if (instance.pmu.isBatteryConnect() == false) {
        return false;
    }
    const int reading = instance.pmu.getBatteryPercent();
    if (reading < 0) {
        return false;
    }
    percent = reading;
    charging = instance.pmu.isCharging();
    return true;
}

// ================================================================================

int watch_board_safe_inset() {
    return 0;
}

// ================================================================================

bool watch_board_has_rtc() {
    return device_online(HW_RTC_ONLINE);
}

// ================================================================================

bool watch_board_rtc_read(std::time_t &utc) {
    if (watch_board_has_rtc() == false) {
        return false;
    }
    // The chip stores a bare calendar with no zone, so the pairing below keeps it
    // on UTC no matter what the C library believes the local zone is.
    const std::tm stored = instance.rtc.getDateTime().toUnixTime();
    const std::time_t stamp = watch_utc_time_from_civil(stored);
    if (stamp < kWatchMinimumValidTime) {
        return false;
    }
    utc = stamp;
    return true;
}

// ================================================================================

bool watch_board_rtc_write(std::time_t utc) {
    if (watch_board_has_rtc() == false || utc < kWatchMinimumValidTime) {
        return false;
    }
    std::tm broken_down{};
    gmtime_r(&utc, &broken_down);
    instance.rtc.setDateTime(broken_down);
    return true;
}

// ================================================================================

// LilyGoLib's own lightSleep() arms every wake pin as active-low, which can
// never fire for the active-high sensor line, so the wake sources are built
// here. Level-triggered GPIO wakeup also takes a per-pin polarity, which the
// ext1 source on this chip does not.
void watch_board_sleep() {
    instance.powerControl(POWER_DISPLAY_BACKLIGHT, false);
    instance.sleepDisplay();

    const bool sensor_online = device_online(HW_BMA423_ONLINE);
    if (sensor_online) {
        // Tilt is the wrist-raise gesture. The step, activity and motion IRQs
        // share this pin, so leaving them armed would wake the watch on every
        // arm swing.
        instance.sensor.disablePedometerIRQ();
        instance.sensor.disableActivityIRQ();
        instance.sensor.disableAnyNoMotionIRQ();
        instance.sensor.disableWakeupIRQ();
        instance.sensor.enableTiltIRQ();
        instance.sensor.readIrqStatus();
        gpio_wakeup_enable(
                static_cast<gpio_num_t>(SENSOR_INT),
                GPIO_INTR_HIGH_LEVEL);
    }
    gpio_wakeup_enable(
            static_cast<gpio_num_t>(TP_INT),
            GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    esp_light_sleep_start();

    gpio_wakeup_disable(static_cast<gpio_num_t>(TP_INT));
    gpio_set_intr_type(static_cast<gpio_num_t>(TP_INT), kTouchAwakeTrigger);
    if (sensor_online) {
        gpio_wakeup_disable(static_cast<gpio_num_t>(SENSOR_INT));
        gpio_set_intr_type(
                static_cast<gpio_num_t>(SENSOR_INT),
                kSensorAwakeTrigger);
        instance.sensor.readIrqStatus();
        instance.sensor.enablePedometerIRQ();
        instance.sensor.enableActivityIRQ();
        instance.sensor.enableAnyNoMotionIRQ();
        instance.sensor.enableWakeupIRQ();
    }

    instance.wakeupDisplay();
    instance.powerControl(POWER_DISPLAY_BACKLIGHT, true);
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
}

#endif
