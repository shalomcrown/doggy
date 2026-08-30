#ifndef SERVO_BOARD_H
#define SERVO_BOARD_H

#include <cstdint>
#include <string>

// ================================================================================

class ServoBoard {
private:
    int bus_fd = -1;
    double frequency = 50.0;
    double maxAngle = 180;
    double minPwmMs = 0.5;
    double maxPwmMs = 2.0;
    std::string lastErrorMessage;

public:
    ServoBoard();
    ServoBoard(int bus, uint8_t address);
    ~ServoBoard();

    bool isOpen() const;
    const std::string &lastError() const;

    double pwmFrequency() const;
    double servoMaxAngle() const;
    double servoMinPwmMs() const;
    double servoMaxPwmMs() const;

    void set_all_pwm(const uint16_t on, const uint16_t off);
    void set_pwm_freq(const double freq_hz);
    void set_pwm(const int channel, const uint16_t on, const uint16_t off);
    void set_pwm_ms(const int channel, const double ms);
    void set_angle(const int channel, const double angleDegrees);
};

#endif
