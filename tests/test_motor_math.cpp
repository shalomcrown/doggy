#include "motor_math.h"

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

    const MotorSignal disabled = motor_signal(1.0, false, false);
    expect(disabled.pwm == 0 && disabled.in1 == false && disabled.in2 == false,
           "disabled motor coasts");

    const MotorSignal invalid = motor_signal(NAN, true, false);
    expect(invalid.pwm == 0 && invalid.in1 == false && invalid.in2 == false,
           "non-finite speed coasts");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
