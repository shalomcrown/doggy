#ifndef PCA9685_H
#define PCA9685_H

#include <cstdint>
#include <string>

// Thin wrapper for the Adafruit PCA9685 registers and basic operations.
// Provides open/close, frequency and per-channel pwm access.

class PCA9685 {
private:
    int fd_ = -1;
    uint8_t address_ = 0;
    double frequency_ = 0.0;
    std::string last_error_;

public:
    PCA9685(int bus, uint8_t address);
    ~PCA9685();

    bool isOpen() const;
    const std::string &lastError() const;

    void set_pwm_freq(double freq_hz);
    void set_pwm(int channel, uint16_t on, uint16_t off);
    void set_all_pwm(uint16_t on, uint16_t off);
};

#endif
