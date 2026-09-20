#ifndef MOTOR_MATH_H
#define MOTOR_MATH_H

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

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

struct ArcadeMix {
    double left = 0.0;
    double right = 0.0;
};

// ================================================================================

inline bool gcs_watchdog_expired(
        const std::optional<std::chrono::steady_clock::time_point> &last_gcs,
        std::chrono::steady_clock::time_point now,
        int timeout_s) {
    if (last_gcs.has_value() == false) {
        return false;
    }
    return now - *last_gcs >= std::chrono::seconds(timeout_s);
}

// ================================================================================

// Arcade mix: add turn to the left side and subtract it from the right, then
// scale both wheels by the same factor if either would leave [-1, 1]. At rest
// a full turn is an in-place spin; at full speed the same stick only slows
// the inside wheels, so steering authority falls as |speed| rises.
// turn_gain_min (0–1) scales that rest spin: 1.0 keeps full pivot authority,
// smaller values tame low-speed steering while full speed still uses full turn.
inline ArcadeMix mix_arcade(
        double speed,
        double turn,
        double turn_gain_min = 1.0) {
    if (std::isfinite(speed) == false || std::isfinite(turn) == false) {
        return {};
    }
    if (std::isfinite(turn_gain_min) == false
            || turn_gain_min < 0.0
            || turn_gain_min > 1.0) {
        turn_gain_min = 1.0;
    }

    const double turn_eff =
            turn * (turn_gain_min + (1.0 - turn_gain_min) * std::abs(speed));
    double left = speed + turn_eff;
    double right = speed - turn_eff;
    const double peak = std::max(std::abs(left), std::abs(right));
    if (peak > 1.0) {
        left /= peak;
        right /= peak;
    }
    return {left, right};
}

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

inline MotorSignal motor_brake_signal() {
    MotorSignal signal;
    signal.pwm = 0;
    signal.in1 = true;
    signal.in2 = true;
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
