#include "servo.h"
#include "servo_board.h"
#include "pwm_math.h"

// ================================================================================

Servo::Servo(ServoBoard &board, int channel, std::string name) :
    board(board),
    channel(channel),
    servoName(std::move(name)),
    angleDegrees(0.0),
    pwmTicks(0) {
}

// ================================================================================

int Servo::id() const {
    return channel;
}

// ================================================================================

const std::string &Servo::name() const {
    return servoName;
}

// ================================================================================

double Servo::angle() const {
    return angleDegrees;
}

// ================================================================================

int Servo::pwm() const {
    return pwmTicks;
}

// ================================================================================

void Servo::setAngle(double degrees) {
    const double maxAngle = board.servoMaxAngle();
    if (degrees < 0.0) {
        degrees = 0.0;
    }

    if (degrees > maxAngle) {
        degrees = maxAngle;
    }

    angleDegrees = degrees;
    pwmTicks = pwm_ticks_from_angle(
            degrees,
            board.pwmFrequency(),
            board.servoMinPwmMs(),
            board.servoMaxPwmMs(),
            maxAngle);
    board.set_pwm(channel, 0, pwmTicks);
}

// ================================================================================

void Servo::off() {
    pwmTicks = 0;
    board.set_pwm(channel, 0, 0);
}

// ================================================================================

void Servo::rebindChannel(int new_channel) {
    if (new_channel == channel) {
        return;
    }

    if (pwmTicks != 0) {
        board.set_pwm(channel, 0, 0);
    }

    channel = new_channel;
    if (pwmTicks != 0) {
        board.set_pwm(channel, 0, pwmTicks);
    }
}
