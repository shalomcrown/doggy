#ifndef SERVO_H
#define SERVO_H

#include <cstdint>
#include <string>

class ServoBoard;

// ================================================================================

class Servo {
private:
    ServoBoard &board;
    int channel;
    std::string servoName;
    double angleDegrees;
    uint16_t pwmTicks;

public:
    Servo(ServoBoard &board, int channel, std::string name);

    int id() const;
    const std::string &name() const;
    double angle() const;
    int pwm() const;

    void setAngle(double degrees);
};

#endif
