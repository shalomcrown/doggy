#ifndef SERVO_BOARD_H
#define SERVO_BOARD_H

#include <cstdint>
#include <string>

// ================================================================================

#include <memory>

class PCA9685;

class ServoBoard {
private:
    std::unique_ptr<PCA9685> pca;
    int bus_fd = -1; // used by legacy board path
    double frequency = 50.0;
    double maxAngle = 180;
    double minPwmMs = 0.5;
    double maxPwmMs = 2.0;
    std::string lastErrorMessage;

public:
    ServoBoard();
    // new constructor accepts optional backend type: "pca9685" or "legacy"; empty -> autodetect
    ServoBoard(int bus, uint8_t address, const std::string &backend_type = std::string());
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
