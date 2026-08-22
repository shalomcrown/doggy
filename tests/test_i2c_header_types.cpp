#include "i2c_interface.hpp"

int main() {
    static_assert(sizeof(uint8_t) == 1, "i2c_interface.hpp must provide uint8_t");
    const uint8_t address = 0x40;
    (void)address;
    return 0;
}
