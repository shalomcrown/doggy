#ifndef MOTOR_MATH_H
#define MOTOR_MATH_H

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

inline constexpr uint16_t kPca9685FullOn = 4096;
inline constexpr uint16_t kPca9685FullOff = 4096;

// ================================================================================

struct MotorSignal {
    uint16_t pwm = 0;
    bool in1 = false;
    bool in2 = false;
};

// ================================================================================

struct MotorPwmWrite {
    int channel = 0;
    uint16_t on = 0;
    uint16_t off = 0;
};

// ================================================================================

struct MotorWritePlan {
    std::array<MotorPwmWrite, 4> writes{};
    std::size_t size = 0;
};

// ================================================================================

inline MotorSignal motor_signal(double speed, bool enabled, bool reverse_wiring) {
    if (enabled == false || std::isfinite(speed) == false || speed == 0.0) {
        return {};
    }

    double magnitude = std::abs(speed);
    if (magnitude > 1.0) {
        magnitude = 1.0;
    }

    MotorSignal signal;
    signal.pwm = static_cast<uint16_t>(std::round(magnitude * 4095.0));
    const bool positive = speed > 0.0;
    signal.in1 = positive != reverse_wiring;
    signal.in2 = signal.in1 == false;
    return signal;
}

// ================================================================================

inline MotorWritePlan motor_write_plan(
        int pwm_channel,
        int in2_channel,
        int in1_channel,
        const MotorSignal &signal) {
    MotorWritePlan plan;
    plan.writes[0] = {pwm_channel, 0, kPca9685FullOff};
    plan.writes[1] = signal.in1
            ? MotorPwmWrite{in1_channel, kPca9685FullOn, 0}
            : MotorPwmWrite{in1_channel, 0, kPca9685FullOff};
    plan.writes[2] = signal.in2
            ? MotorPwmWrite{in2_channel, kPca9685FullOn, 0}
            : MotorPwmWrite{in2_channel, 0, kPca9685FullOff};
    plan.size = 3;
    if (signal.pwm != 0) {
        plan.writes[3] = {pwm_channel, 0, signal.pwm};
        plan.size = 4;
    }
    return plan;
}

#endif
