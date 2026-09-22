#!/usr/bin/env bash
# Contract tests for Waveshare C6 PMIC, RTC, and IMU (no hardware).
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BOARD="$ROOT/watch/src/board_waveshare_c6.cpp"
INI="$ROOT/watch/platformio.ini"
S3_INI_ENV="$ROOT/watch/platformio.ini"
FAILS=0

# ================================================================================

pass() { printf 'PASS %s\n' "$1"; }

# ================================================================================

fail() { printf 'FAIL %s\n' "$1"; FAILS=$((FAILS + 1)); }

# ================================================================================

# The pinout names AXP2101 on the shared I2C bus; the status bar must not keep
# fabricating an empty gauge once that path exists.
if grep -q 'XPowersLib.h' "$BOARD" \
        && grep -q 'AXP2101_SLAVE_ADDRESS' "$BOARD" \
        && grep -q 'getBatteryPercent' "$BOARD" \
        && grep -q 'isCharging' "$BOARD"; then
    pass "C6 battery reads AXP2101 through XPowersLib"
else
    fail "C6 battery reads AXP2101 through XPowersLib"
fi

if grep -A2 'bool watch_board_battery' "$BOARD" | grep -q 'return false'; then
    # A one-line stub is the old unimplemented path. A real driver may still
    # return false when the pack is missing, so require the gauge call above.
    if grep -q 'getBatteryPercent' "$BOARD"; then
        pass "C6 battery is more than an unimplemented stub"
    else
        fail "C6 battery is more than an unimplemented stub"
    fi
else
    pass "C6 battery is more than an unimplemented stub"
fi

if grep -q 'SensorPCF85063' "$BOARD" \
        && grep -q 'watch_utc_time_from_civil' "$BOARD" \
        && grep -q 'rtc.setDateTime' "$BOARD"; then
    pass "C6 RTC reads and writes PCF85063 as UTC"
else
    fail "C6 RTC reads and writes PCF85063 as UTC"
fi

if grep -q 'bool watch_board_has_rtc' "$BOARD" \
        && grep -A3 'bool watch_board_has_rtc' "$BOARD" | grep -q 'return false;'; then
    fail "C6 reports a calendar chip when the PCF85063 came up"
else
    pass "C6 reports a calendar chip when the PCF85063 came up"
fi

if grep -q 'SensorQMI8658' "$BOARD" \
        && grep -q 'configWakeOnMotion' "$BOARD" \
        && grep -q 'kImuInt1 = 16' "$BOARD" \
        && grep -q '0x6B' "$BOARD"; then
    pass "C6 arms QMI8658 wake-on-motion on INT1"
else
    fail "C6 arms QMI8658 wake-on-motion on INT1"
fi

if grep -A90 'void watch_board_sleep' "$BOARD" | grep -q 'kImuInt1'; then
    pass "C6 light sleep can wake from the IMU interrupt"
else
    fail "C6 light sleep can wake from the IMU interrupt"
fi

# Wake-on-motion toggles INT1 rather than resting at a level, so a hardcoded
# level arms whatever the pin already shows and light sleep returns at once.
if grep -A90 'void watch_board_sleep' "$BOARD" \
        | grep -q 'watch_toggling_wake_level'; then
    pass "C6 samples the IMU line instead of assuming its level"
else
    fail "C6 samples the IMU line instead of assuming its level"
fi

# Light sleep stops clocking the USB-Serial/JTAG peripheral, which locks out
# flashing until someone touches the watch.
if grep -A90 'void watch_board_sleep' "$BOARD" \
        | grep -q 'watch_sleep_uses_light_sleep(HWCDC::isPlugged())'; then
    pass "C6 stays out of light sleep while a USB host is attached"
else
    fail "C6 stays out of light sleep while a USB host is attached"
fi

# The wake-on-motion event lives in STATUS1 (0x2F), which getStatusRegister
# reads. getIrqStatus reads STATUS_INT (0x2D) and leaves that event uncleared.
if grep -q 'imu\.getStatusRegister' "$BOARD"; then
    pass "C6 clears wake-on-motion through STATUS1"
else
    fail "C6 clears wake-on-motion through STATUS1"
fi

if grep -q 'imu\.getIrqStatus' "$BOARD"; then
    fail "C6 does not clear wake-on-motion through the wrong register"
else
    pass "C6 does not clear wake-on-motion through the wrong register"
fi

if grep -A90 'void watch_board_sleep' "$BOARD" \
        | grep -q 'esp_sleep_get_gpio_wakeup_status'; then
    pass "C6 reports which GPIO ended the sleep"
else
    fail "C6 reports which GPIO ended the sleep"
fi

if grep -A20 '^\[env:waveshare-c6\]' "$INI" | grep -q 'XPowersLib' \
        && grep -A20 '^\[env:waveshare-c6\]' "$INI" | grep -q 'SensorLib' \
        && grep -A20 '^\[env:waveshare-c6\]' "$INI" | grep -q 'XPOWERS_CHIP_AXP2101'; then
    pass "C6 PlatformIO env pulls XPowersLib and SensorLib"
else
    fail "C6 PlatformIO env pulls XPowersLib and SensorLib"
fi

if grep -A30 '^\[env:twatch-s3\]' "$S3_INI_ENV" | grep -q 'XPowersLib' \
        || grep -A30 '^\[env:twatch-s3\]' "$S3_INI_ENV" | grep -q 'SensorLib'; then
    fail "S3 env does not add the C6-only power and sensor libraries"
else
    pass "S3 env does not add the C6-only power and sensor libraries"
fi

if grep -q 'ES8311\|ES7210\|PA_CTRL' "$BOARD"; then
    fail "C6 audio codec stays out of this slice"
else
    pass "C6 audio codec stays out of this slice"
fi

if [ "$FAILS" -ne 0 ]; then
    printf '%d test(s) failed\n' "$FAILS" >&2
    exit 1
fi

echo "All C6 peripheral tests passed."
