#include "pca9685.h"

#include <cmath>
#include <system_error>
#include <unistd.h>

#include "i2c_interface.hpp"

// Registers/etc:
constexpr uint8_t MODE1              = 0x00;
constexpr uint8_t MODE2              = 0x01;
constexpr uint8_t SUBADR1            = 0x02;
constexpr uint8_t SUBADR2            = 0x03;
constexpr uint8_t SUBADR3            = 0x04;
constexpr uint8_t PRESCALE           = 0xFE;
constexpr uint8_t LED0_ON_L          = 0x06;
constexpr uint8_t LED0_ON_H          = 0x07;
constexpr uint8_t LED0_OFF_L         = 0x08;
constexpr uint8_t LED0_OFF_H         = 0x09;
constexpr uint8_t ALL_LED_ON_L       = 0xFA;
constexpr uint8_t ALL_LED_ON_H       = 0xFB;
constexpr uint8_t ALL_LED_OFF_L      = 0xFC;
constexpr uint8_t ALL_LED_OFF_H      = 0xFD;

// Bits:
constexpr uint8_t RESTART            = 0x80;
constexpr uint8_t SLEEP              = 0x10;
constexpr uint8_t ALLCALL            = 0x01;
constexpr uint8_t INVRT              = 0x10;
constexpr uint8_t OUTDRV             = 0x04;

PCA9685::PCA9685(int bus, uint8_t address) : fd_(-1), address_(address) {
    try {
        fd_ = openBus(i2c_device_path(bus), address);
        writeRegisterByte(fd_, MODE2, OUTDRV);
        usleep(5'000);
        writeRegisterByte(fd_, MODE1, ALLCALL);
        usleep(5'000);
        auto mode1_val = readRegisterByte(fd_, MODE1);
        mode1_val &= ~SLEEP;
        writeRegisterByte(fd_, MODE1, mode1_val);
        usleep(5'000);
        set_pwm_freq(50.0);
        set_all_pwm(0, 0);
    } catch (const std::system_error &ex) {
        last_error_ = ex.what();
        fd_ = -1;
    }
}

PCA9685::~PCA9685() {
    if (fd_ < 0) {
        return;
    }
    set_all_pwm(0, 0);
    closeBus(fd_);
}

bool PCA9685::isOpen() const {
    return fd_ >= 0;
}

const std::string &PCA9685::lastError() const {
    return last_error_;
}

void PCA9685::set_all_pwm(uint16_t on, uint16_t off) {
    if (isOpen() == false) {
        return;
    }

    writeRegisterByte(fd_, ALL_LED_ON_L, on & 0xFF);
    writeRegisterByte(fd_, ALL_LED_ON_H, on >> 8);
    writeRegisterByte(fd_, ALL_LED_OFF_L, off & 0xFF);
    writeRegisterByte(fd_, ALL_LED_OFF_H, off >> 8);
}

void PCA9685::set_pwm_freq(double freq_hz) {
    frequency_ = freq_hz;
    if (isOpen() == false) {
        return;
    }

    auto prescaleval = 2.5e7; //    # 25MHz
    prescaleval /= 4096.0; //       # 12-bit
    prescaleval /= freq_hz;
    prescaleval -= 1.0;

    auto prescale = static_cast<int>(std::round(prescaleval));

    const auto oldmode = readRegisterByte(fd_, MODE1);

    auto newmode = (oldmode & 0x7F) | SLEEP;

    writeRegisterByte(fd_, MODE1, newmode);
    writeRegisterByte(fd_, PRESCALE, prescale);
    writeRegisterByte(fd_, MODE1, oldmode);
    usleep(5'000);
    writeRegisterByte(fd_, MODE1, oldmode | RESTART);
}

void PCA9685::set_pwm(int channel, uint16_t on, uint16_t off) {
    if (isOpen() == false) {
        return;
    }

    const auto channel_offset = 4 * channel;
    writeRegisterByte(fd_, LED0_ON_L + channel_offset, on & 0xFF);
    writeRegisterByte(fd_, LED0_ON_H + channel_offset, on >> 8);
    writeRegisterByte(fd_, LED0_OFF_L + channel_offset, off & 0xFF);
    writeRegisterByte(fd_, LED0_OFF_H + channel_offset, off >> 8);
}
