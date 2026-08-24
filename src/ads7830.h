#ifndef ADS7830_H
#define ADS7830_H

#include <cstdint>

inline constexpr uint8_t kAds7830Address = 0x48;
inline constexpr uint8_t kAds7830CommandBase = 0x84;
inline constexpr int kAds7830BatteryChannel = 0;
inline constexpr int kAds7830BatterySamples = 9;
inline constexpr double kAds7830Vref = 5.0;
inline constexpr double kAds7830FullScale = 255.0;
inline constexpr double kAds7830BatteryDivider = 2.0;

// ================================================================================

inline uint8_t ads7830_command(int channel) {
    const int nibble = ((channel << 2) | (channel >> 1)) & 0x07;
    return static_cast<uint8_t>(kAds7830CommandBase | (nibble << 4));
}

// ================================================================================

inline double ads7830_battery_voltage_v(uint8_t raw) {
    return (raw / kAds7830FullScale) * kAds7830Vref * kAds7830BatteryDivider;
}

// ================================================================================

class Ads7830 {
public:
    Ads7830();
    int bus_fd;

    bool isOpen() const;
    uint8_t readChannel(int channel);
    double readBatteryVoltage();
};

#endif
