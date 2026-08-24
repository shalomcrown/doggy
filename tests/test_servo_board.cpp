#include "servo_board.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

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
    try {
        ServoBoard board;
        expect(true, "ServoBoard constructor does not throw");
        if (board.isOpen() == false) {
            expect(board.lastError().find("Could not open i2c bus") != std::string::npos,
                   "closed board records i2c open error");
        }
    } catch (const std::exception &) {
        expect(false, "ServoBoard constructor does not throw");
    }

    if (failures != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
