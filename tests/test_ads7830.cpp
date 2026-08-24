#include "ads7830.h"

#include <cmath>
#include <iostream>

// ================================================================================

static int failures = 0;

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
    expect(ads7830_command(0) == 0x84, "channel 0 command is 0x84");
    expect(std::abs(ads7830_battery_voltage_v(0) - 0.0) < 1e-9,
           "raw 0 is 0.0 V");
    expect(std::abs(ads7830_battery_voltage_v(255) - 10.0) < 1e-9,
           "raw 255 is 10.0 V");
    expect(std::abs(ads7830_battery_voltage_v(128) - (128.0 / 255.0 * 5.0 * 2.0))
                   < 1e-9,
           "raw 128 matches Freenove scale");

    if (failures != 0) {
        return 1;
    }

    return 0;
}
