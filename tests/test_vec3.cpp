#include "dog_status.h"

#include <iostream>

static int failures = 0;

static void expect(bool cond, const char *name) {
    if (cond) {
        std::cout << "PASS " << name << std::endl;
        return;
    }

    std::cout << "FAIL " << name << std::endl;
    failures += 1;
}

int main() {
    const Vec3 a{1.0, 2.0, 3.0};
    const Vec3 b{0.5, -2.0, 0.25};
    const Vec3 sum = a + b;
    expect(sum.x == 1.5 && sum.y == 0.0 && sum.z == 3.25, "Vec3 addition");

    const Vec3 zero{};
    expect(zero.x == 0.0 && zero.y == 0.0 && zero.z == 0.0, "Vec3 default is zero");

    if (failures != 0) {
        return 1;
    }

    return 0;
}
