#include "doggy.h"

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
    Dog dog;
    dog.poll();
    const DogStatus first = dog.getStatus();
    dog.poll();
    const DogStatus second = dog.getStatus();

    expect(true, "poll does not throw");
    if (first.imu.ok) {
        expect(second.imu.ok, "open IMU stays ok across polls");
    } else {
        expect(second.imu.ok == false, "closed IMU stays not ok across polls");
    }

    if (failures != 0) {
        return 1;
    }

    return 0;
}
