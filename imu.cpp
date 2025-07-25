#include "imu.h"
#include "i2c_interface.hpp"

// =================================================================
Imu::Imu() :  bus_fd(openBus("/dev/i2c-1", 0x68)) {

    writeRegisterByte(bus_fd, 0x6B, 0); // Reset
    writeRegisterByte(bus_fd, 0x1C, 1 << 3); // Accelerometer range to 2g
    writeRegisterByte(bus_fd, 0x1B, 0); // Gyto 250 degrees/s
}

// =================================================================

dlib::vector<double> Imu::readAccelerometer() {
    uint8_t data[6];
    readRegisterBlock(bus_fd, 0x3B, 6, data);

    double x = ((int16_t)((data[0] << 8) + (data[1] & 0xFF))) * 2.0 / accelerometerSensitivityPerBit;
    double y = ((int16_t)((data[2] << 8) + (data[3] & 0xFF))) * 2.0 / accelerometerSensitivityPerBit;
    double z = ((int16_t)((data[4] << 8) + (data[5] & 0xFF))) * 2.0 / accelerometerSensitivityPerBit;

    return dlib::vector<double>(x, y, z) + acceleratorOffset;
}

// =================================================================
double Imu::readTemperature() {
    uint8_t data[2];
    readRegisterBlock(bus_fd, 0x41, 2, data);

    return ((int16_t)((data[0] << 8) + (data[1] & 0xFF))) / 340.0 + 36.5;
}

// =================================================================

dlib::vector<double> Imu::readGyro() {
    uint8_t data[6];
    readRegisterBlock(bus_fd, 0x43, 6, data);

    double x = ((int16_t)((data[0] << 8) + (data[1] & 0xFF))) * 2.0 / gyroSensitivityPerBit;
    double y = ((int16_t)((data[2] << 8) + (data[3] & 0xFF))) * 2.0 / gyroSensitivityPerBit;
    double z = ((int16_t)((data[4] << 8) + (data[5] & 0xFF))) * 2.0 / gyroSensitivityPerBit;

    return dlib::vector<double>(x, y, z) + gyroOffset;
}
