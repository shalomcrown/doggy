#include "ads7830.h"
#include "i2c_interface.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <system_error>

// ================================================================================

Ads7830::Ads7830() : Ads7830(1, kAds7830Address) {
}

// ================================================================================

Ads7830::Ads7830(int bus, uint8_t address) : bus_fd(-1) {
    try {
        bus_fd = openBus(i2c_device_path(bus), address);
    } catch (const std::system_error &) {
        bus_fd = -1;
    }
}

// ================================================================================

bool Ads7830::isOpen() const {
    return bus_fd >= 0;
}

// ================================================================================

uint8_t Ads7830::readChannel(int channel) {
    writeByte(bus_fd, ads7830_command(channel));
    return readByte(bus_fd);
}

// ================================================================================

double Ads7830::readBatteryVoltage() {
    std::array<uint8_t, kAds7830BatterySamples> samples{};
    for (int i = 0; i < kAds7830BatterySamples; ++i) {
        samples[static_cast<size_t>(i)] = readChannel(kAds7830BatteryChannel);
    }

    std::sort(samples.begin(), samples.end());
    return ads7830_battery_voltage_v(samples[kAds7830BatterySamples / 2]);
}
