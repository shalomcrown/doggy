#include "utils.h"

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
    unsigned char empty[] = {0};
    expect(to_hex(empty, 0) == "", "to_hex empty is empty string");

    const unsigned char one[] = {0x0f};
    expect(to_hex(one, 1) == "0f", "to_hex one byte is lowercase");

    const unsigned char two[] = {0x00, 0xff};
    expect(to_hex(two, 2) == "00ff", "to_hex two bytes");

    expect(hashes_equal("abc", "abc"), "hashes_equal same strings");
    expect(hashes_equal("abc", "abd") == false, "hashes_equal different last byte");
    expect(hashes_equal("ab", "abc") == false, "hashes_equal different lengths");
    expect(hashes_equal(nullptr, "abc") == false, "hashes_equal null left");
    expect(hashes_equal("abc", nullptr) == false, "hashes_equal null right");

    if (failures != 0) {
        return 1;
    }

    return 0;
}
