#include "watch_rover_status.h"

#include <cstring>
#include <cstdlib>
#include <iostream>

static int failures = 0;

static void expect(bool condition, const char *name) {
    if (condition) {
        return;
    }

    std::cerr << "FAIL " << name << std::endl;
    failures += 1;
}

int main() {
    const char *rover = R"({"type":"ROVER","version":"1"})";
    expect(
            watch_rover_status_is_rover(rover, std::strlen(rover)),
            "compact ROVER type is detected");
    const char *dog = R"({"type":"DOG"})";
    expect(
            watch_rover_status_is_rover(dog, std::strlen(dog)) == false,
            "DOG type is rejected");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
