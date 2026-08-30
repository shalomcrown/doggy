#include "imu.h"
#include "i2c_interface.hpp"

#include <system_error>

// =================================================================
Imu::Imu() : Imu(1, 0x68) {
}

// =================================================================

Imu::Imu(int bus, uint8_t address) : bus_fd(-1) {
    try {
        bus_fd = openBus(i2c_device_path(bus), address);
        writeRegisterByte(bus_fd, 0x6B, 0); // Reset
        writeRegisterByte(bus_fd, 0x1C, 1 << 3); // Accelerometer range to 2g
        writeRegisterByte(bus_fd, 0x1B, 0); // Gyto 250 degrees/s
    } catch (const std::system_error &) {
        bus_fd = -1;
    }
}

// =================================================================

Vec3 Imu::readAccelerometer() {
    if (bus_fd < 0) {
        return {};
    }

    uint8_t data[6];
    readRegisterBlock(bus_fd, 0x3B, 6, data);

    double x = ((int16_t)((data[0] << 8) + (data[1] & 0xFF))) * 2.0 / accelerometerSensitivityPerBit;
    double y = ((int16_t)((data[2] << 8) + (data[3] & 0xFF))) * 2.0 / accelerometerSensitivityPerBit;
    double z = ((int16_t)((data[4] << 8) + (data[5] & 0xFF))) * 2.0 / accelerometerSensitivityPerBit;

    return Vec3{x, y, z} + acceleratorOffset;
}

// =================================================================
double Imu::readTemperature() {
    if (bus_fd < 0) {
        return 0.0;
    }

    uint8_t data[2];
    readRegisterBlock(bus_fd, 0x41, 2, data);

    return ((int16_t)((data[0] << 8) + (data[1] & 0xFF))) / 340.0 + 36.5;
}

// =================================================================

Vec3 Imu::readGyro() {
    if (bus_fd < 0) {
        return {};
    }

    uint8_t data[6];
    readRegisterBlock(bus_fd, 0x43, 6, data);

    double x = ((int16_t)((data[0] << 8) + (data[1] & 0xFF))) * 2.0 / gyroSensitivityPerBit;
    double y = ((int16_t)((data[2] << 8) + (data[3] & 0xFF))) * 2.0 / gyroSensitivityPerBit;
    double z = ((int16_t)((data[4] << 8) + (data[5] & 0xFF))) * 2.0 / gyroSensitivityPerBit;

    return Vec3{x, y, z} + gyroOffset;
}

// ================================================================================

bool Imu::isOpen() const {
    return bus_fd >= 0;
}
