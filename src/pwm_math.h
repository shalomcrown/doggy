#ifndef PWM_MATH_H
#define PWM_MATH_H

#include <cmath>
#include <cstdint>

// ================================================================================

inline uint16_t pwm_ticks_from_angle(
        const double angle_degrees,
        const double frequency_hz,
        const double min_pwm_ms,
        const double max_pwm_ms,
        const double max_angle) {
    double angle = angle_degrees;
    if (angle < 0.0) {
        angle = 0.0;
    }

    if (angle > max_angle) {
        angle = max_angle;
    }

    const double ms = max_pwm_ms * angle / max_angle + min_pwm_ms;
    const double period_ms = 1000.0 / frequency_hz;
    const double bits_per_ms = 4096.0 / period_ms;
    const double bits = ms * bits_per_ms;
    if (bits < 0.0) {
        return 0;
    }

    if (bits > 4095.0) {
        return 4095;
    }

    return static_cast<uint16_t>(std::round(bits));
}

#endif

