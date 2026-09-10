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

ServoBoard::ServoBoard(int bus, uint8_t address) : pca(nullptr) {
    try {
        pca = std::make_unique<PCA9685>(bus, address);
        if (pca->isOpen() == false) {
            lastErrorMessage = pca->lastError();
            pca.reset();
            return;
        }
        set_pwm_freq(50.0);
        set_all_pwm(0, 0);
    } catch (const std::system_error &ex) {
        lastErrorMessage = ex.what();
        pca.reset();
    }
}

// ================================================================================

bool ServoBoard::isOpen() const {
    return pca != nullptr && pca->isOpen();
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
    pca->set_all_pwm(on, off);
}

// ================================================================================

void ServoBoard::set_pwm_freq(const double freq_hz) {
    frequency = freq_hz;
    if (isOpen() == false) {
        return;
    }
    pca->set_pwm_freq(freq_hz);
}

// ================================================================================

void ServoBoard::set_pwm(const int channel, const uint16_t on, const uint16_t off) {
    if (isOpen() == false) {
        return;
    }
    pca->set_pwm(channel, on, off);
}

// ================================================================================

void ServoBoard::set_pwm_ms(const int channel, const double ms) {
    auto period_ms = 1000.0 / frequency;
    auto bits_per_ms = 4096 / period_ms;
    auto bits = ms * bits_per_ms;
    set_pwm(channel, 0, bits);
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
    if (pca == nullptr || pca->isOpen() == false) {
        return;
    }

    set_all_pwm(0, 0);
}
