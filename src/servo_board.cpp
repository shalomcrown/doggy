#include <unistd.h>
#include <cmath>
#include <iostream>
#include <system_error>

#include "pwm_math.h"
#include "servo_board.h"
#include "pca9685.h"

// ================================================================================

ServoBoard::ServoBoard() : ServoBoard(1, 0x40) {
}

// ================================================================================

ServoBoard::ServoBoard(int bus, uint8_t address, const std::string &backend_type) : pca(nullptr), bus_fd(-1) {
    auto try_pca = [&]() -> bool {
        try {
            pca = std::make_unique<PCA9685>(bus, address);
            if (pca->isOpen() == false) {
                lastErrorMessage = pca->lastError();
                pca.reset();
                return false;
            }
            set_pwm_freq(50.0);
            set_all_pwm(0, 0);
            return true;
        } catch (const std::system_error &ex) {
            lastErrorMessage = ex.what();
            pca.reset();
            return false;
        }
    };

    auto try_legacy = [&]() -> bool {
        try {
            bus_fd = openBus(i2c_device_path(bus), address);
            // initialize similar to previous implementation
            writeRegisterByte(bus_fd, MODE2, OUTDRV);
            usleep(5'000);
            writeRegisterByte(bus_fd, MODE1, ALLCALL);
            usleep(5'000);
            auto mode1_val = readRegisterByte(bus_fd, MODE1);
            mode1_val &= ~SLEEP;
            writeRegisterByte(bus_fd, MODE1, mode1_val);
            usleep(5'000);
            set_pwm_freq(50.0);
            set_all_pwm(0, 0);
            return true;
        } catch (const std::system_error &ex) {
            lastErrorMessage = ex.what();
            if (bus_fd >= 0) { closeBus(bus_fd); bus_fd = -1; }
            return false;
        }
    };

    if (backend_type.empty()) {
        // autodetect: prefer PCA9685, fall back to legacy
        if (try_pca()) return;
        try_legacy();
    } else if (backend_type == "pca9685") {
        try_pca();
    } else if (backend_type == "legacy") {
        try_legacy();
    } else {
        lastErrorMessage = "unknown servo backend type: " + backend_type;
    }
}

// ================================================================================

bool ServoBoard::isOpen() const {
    if (pca != nullptr) return pca->isOpen();
    return bus_fd >= 0;
}

// ================================================================================

const std::string &ServoBoard::lastError() const {
    return lastErrorMessage;
}

// ================================================================================

double ServoBoard::pwmFrequency() const {
    return frequency;
}

// ================================================================================

double ServoBoard::servoMaxAngle() const {
    return maxAngle;
}

// ================================================================================

double ServoBoard::servoMinPwmMs() const {
    return minPwmMs;
}

// ================================================================================

double ServoBoard::servoMaxPwmMs() const {
    return maxPwmMs;
}

// ================================================================================

void ServoBoard::set_all_pwm(const uint16_t on, const uint16_t off) {
    if (isOpen() == false) {
        return;
    }
    if (pca != nullptr) {
        pca->set_all_pwm(on, off);
        return;
    }
    writeRegisterByte(bus_fd, ALL_LED_ON_L, on & 0xFF);
    writeRegisterByte(bus_fd, ALL_LED_ON_H, on >> 8);
    writeRegisterByte(bus_fd, ALL_LED_OFF_L, off & 0xFF);
    writeRegisterByte(bus_fd, ALL_LED_OFF_H, off >> 8);
}

// ================================================================================

void ServoBoard::set_pwm_freq(const double freq_hz) {
    frequency = freq_hz;
    if (isOpen() == false) {
        return;
    }
    if (pca != nullptr) {
        pca->set_pwm_freq(freq_hz);
        return;
    }

    auto prescaleval = 2.5e7; //    # 25MHz
    prescaleval /= 4096.0; //       # 12-bit
    prescaleval /= freq_hz;
    prescaleval -= 1.0;

    auto prescale = static_cast<int>(std::round(prescaleval));

    const auto oldmode = readRegisterByte(bus_fd, MODE1);

    auto newmode = (oldmode & 0x7F) | SLEEP;

    writeRegisterByte(bus_fd, MODE1, newmode);
    writeRegisterByte(bus_fd, PRESCALE, prescale);
    writeRegisterByte(bus_fd, MODE1, oldmode);
    usleep(5'000);
    writeRegisterByte(bus_fd, MODE1, oldmode | RESTART);
}

// ================================================================================

void ServoBoard::set_pwm(const int channel, const uint16_t on, const uint16_t off) {
    if (isOpen() == false) {
        return;
    }
    if (pca != nullptr) {
        pca->set_pwm(channel, on, off);
        return;
    }

    const auto channel_offset = 4 * channel;
    writeRegisterByte(bus_fd, LED0_ON_L + channel_offset, on & 0xFF);
    writeRegisterByte(bus_fd, LED0_ON_H + channel_offset, on >> 8);
    writeRegisterByte(bus_fd, LED0_OFF_L + channel_offset, off & 0xFF);
    writeRegisterByte(bus_fd, LED0_OFF_H + channel_offset, off >> 8);
}

// ================================================================================

void ServoBoard::set_pwm_ms(const int channel, const double ms) {
    auto period_ms = 1000.0 / frequency;
    auto bits_per_ms = 4096 / period_ms;
    auto bits = ms * bits_per_ms;
    set_pwm(channel, 0, static_cast<uint16_t>(bits));
}

// ================================================================================

void ServoBoard::set_angle(const int channel, const double angleDegrees) {
    std::cout << "Set andgle: " << angleDegrees << " To channel: " << channel << std::endl;
    const uint16_t ticks = pwm_ticks_from_angle(
            angleDegrees, frequency, minPwmMs, maxPwmMs, maxAngle);
    set_pwm(channel, 0, ticks);
}

// ================================================================================

ServoBoard::~ServoBoard() {
    if (pca != nullptr && pca->isOpen()) {
        set_all_pwm(0, 0);
    }
    if (bus_fd >= 0) {
        set_all_pwm(0, 0);
        closeBus(bus_fd);
        bus_fd = -1;
    }
}
