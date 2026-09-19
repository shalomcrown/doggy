#include "motor_math.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>

static int failures = 0;

// ================================================================================

static void expect(bool cond, const char *name) {
    if (cond) {
        std::cout << "PASS " << name << std::endl;
        return;
    }
    std::cout << "FAIL " << name << std::endl;
    failures += 1;
}

// ================================================================================

int main() {
    const MotorSignal full = motor_signal(1.0, true, false);
    expect(full.pwm == 4095, "full speed uses 4095 PWM ticks");
    expect(full.in1 && full.in2 == false, "positive speed drives IN1 high");
    const MotorWritePlan full_plan = motor_write_plan(2, 3, 4, full);
    expect(full_plan.size == 4, "moving motor has four ordered register writes");
    expect(full_plan.writes[0].channel == 2
                    && full_plan.writes[0].on == 0
                    && full_plan.writes[0].off == 4096,
           "motor PWM is disabled before changing direction");
    expect(full_plan.writes[1].channel == 4
                    && full_plan.writes[1].on == 4096
                    && full_plan.writes[2].channel == 3
                    && full_plan.writes[2].off == 4096,
           "configured IN1 and IN2 channels receive forward levels");
    expect(full_plan.writes[3].channel == 2
                    && full_plan.writes[3].off == 4095,
           "PWM duty is applied after direction pins");

    const MotorSignal half_reverse = motor_signal(-0.5, true, false);
    expect(half_reverse.pwm == 2048, "half speed rounds to 2048 PWM ticks");
    expect(half_reverse.in1 == false && half_reverse.in2,
           "negative speed drives IN2 high");

    const MotorSignal inverted = motor_signal(1.0, true, true);
    expect(inverted.in1 == false && inverted.in2,
           "configured reverse swaps IN1 and IN2");

    const MotorSignal zero = motor_signal(0.0, true, false);
    expect(zero.pwm == 0 && zero.in1 == false && zero.in2 == false,
           "zero speed coasts");
    const MotorWritePlan coast_plan = motor_write_plan(2, 3, 4, zero);
    expect(coast_plan.size == 3
                    && coast_plan.writes[0].off == 4096
                    && coast_plan.writes[1].off == 4096
                    && coast_plan.writes[2].off == 4096,
           "coast fully disables PWM, IN1, and IN2");

    const MotorSignal brake = motor_brake_signal();
    expect(brake.pwm == 0 && brake.in1 && brake.in2,
           "brake drives both IN lines high");
    const MotorWritePlan brake_plan = motor_write_plan(2, 3, 4, brake);
    expect(brake_plan.size == 3
                    && brake_plan.writes[1].on == 4096
                    && brake_plan.writes[2].on == 4096,
           "brake sets both direction inputs high");

    const MotorSignal disabled = motor_signal(1.0, false, false);
    expect(disabled.pwm == 0 && disabled.in1 == false && disabled.in2 == false,
           "disabled motor coasts");

    const MotorSignal invalid = motor_signal(NAN, true, false);
    expect(invalid.pwm == 0 && invalid.in1 == false && invalid.in2 == false,
           "non-finite speed coasts");

    const ArcadeMix straight = mix_arcade(0.5, 0.0);
    expect(straight.left == 0.5 && straight.right == 0.5,
           "zero turn leaves both sides at speed");

    const ArcadeMix pivot = mix_arcade(0.0, 1.0);
    expect(pivot.left == 1.0 && pivot.right == -1.0,
           "full turn at rest is an in-place spin");

    const ArcadeMix reverse_pivot = mix_arcade(0.0, -1.0);
    expect(reverse_pivot.left == -1.0 && reverse_pivot.right == 1.0,
           "negative turn at rest spins the other way");

    const ArcadeMix cruise = mix_arcade(1.0, 1.0);
    expect(cruise.left == 1.0 && cruise.right == 0.0,
           "full speed and full turn desaturates to inside-wheel stop");

    const ArcadeMix reverse_cruise = mix_arcade(-1.0, 1.0);
    expect(reverse_cruise.left == 0.0 && reverse_cruise.right == -1.0,
           "full reverse and full turn desaturates to inside-wheel stop");

    const ArcadeMix curve = mix_arcade(0.5, 0.75);
    expect(curve.left == 1.0 && std::abs(curve.right + 0.2) < 1e-12,
           "overspeed mix keeps the left/right ratio");

    const ArcadeMix nan_mix = mix_arcade(NAN, 1.0);
    expect(nan_mix.left == 0.0 && nan_mix.right == 0.0,
           "non-finite mix coasts");

    const auto now = std::chrono::steady_clock::time_point{
        std::chrono::seconds(10)
    };
    expect(gcs_watchdog_expired({}, now, 3) == false,
           "unarmed GCS watchdog does not expire");
    expect(gcs_watchdog_expired(now - std::chrono::milliseconds(2999), now, 3)
                   == false,
           "GCS watchdog remains live inside its timeout");
    expect(gcs_watchdog_expired(now - std::chrono::seconds(3), now, 3),
           "GCS watchdog expires at its timeout boundary");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
