#include <unistd.h>
#include <cmath>
#include <linux/i2c.h>
#include <i2c/smbus.h>
#include <iostream>

#include "i2c_interface.hpp"
#include "servo.h"



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


// Thanks to: https://github.com/barulicm/PiPCA9685

// =============================================================================

Servo::Servo() : bus_fd(openBus("/dev/i2c-1", 0x40)) {
    writeRegisterByte(bus_fd, MODE2, OUTDRV);
    usleep(5'000);
    writeRegisterByte(bus_fd, MODE1, ALLCALL);
    usleep(5'000);
    auto mode1_val = readRegisterByte(bus_fd, MODE1);
    mode1_val &= ~SLEEP;
    writeRegisterByte(bus_fd, MODE1, mode1_val);
    usleep(5'000);
    set_pwm_freq(50.0);
    set_all_pwm(0,0);
}

// =============================================================================

void Servo::set_all_pwm(const uint16_t on, const uint16_t off) {
  writeRegisterByte(bus_fd, ALL_LED_ON_L, on & 0xFF);
  writeRegisterByte(bus_fd, ALL_LED_ON_H, on >> 8);
  writeRegisterByte(bus_fd, ALL_LED_OFF_L, off & 0xFF);
  writeRegisterByte(bus_fd, ALL_LED_OFF_H, off >> 8);
}

// =============================================================================

void Servo::set_pwm_freq(const double freq_hz) {
  frequency = freq_hz;

  auto prescaleval = 2.5e7; //    # 25MHz
  prescaleval /= 4096.0; //       # 12-bit
  prescaleval /= freq_hz;
  prescaleval -= 1.0;

  auto prescale = static_cast<int>(std::round(prescaleval));

  const auto oldmode = readRegisterByte(bus_fd, MODE1);

  auto newmode = (oldmode & 0x7F) | SLEEP;

  writeRegisterByte(bus_fd,MODE1, newmode);
  writeRegisterByte(bus_fd,PRESCALE, prescale);
  writeRegisterByte(bus_fd,MODE1, oldmode);
  usleep(5'000);
  writeRegisterByte(bus_fd,MODE1, oldmode | RESTART);
}

// =============================================================================


void Servo::set_pwm(const int channel, const uint16_t on, const uint16_t off) {
  const auto channel_offset = 4 * channel;
  writeRegisterByte(bus_fd, LED0_ON_L + channel_offset, on & 0xFF);
  writeRegisterByte(bus_fd, LED0_ON_H + channel_offset, on >> 8);
  writeRegisterByte(bus_fd, LED0_OFF_L + channel_offset, off & 0xFF);
  writeRegisterByte(bus_fd, LED0_OFF_H + channel_offset, off >> 8);
}

// =============================================================================

void Servo::set_pwm_ms(const int channel, const double ms) {
  auto period_ms = 1000.0 / frequency;
  auto bits_per_ms = 4096 / period_ms;
  auto bits = ms * bits_per_ms;
  set_pwm(channel, 0, bits);
}

// =============================================================================

void Servo::set_angle(const int channel, const double angleDegrees) {
    std::cout << "Set andgle: " << angleDegrees << " To channel: " << channel << std::endl;
    double value = maxPwmMs * angleDegrees / maxAngle + minPwmMs;
    set_pwm_ms(channel,  value);
}

// =============================================================================

Servo::~Servo() {
    set_all_pwm(0,0);
    closeBus(bus_fd);
}
