#include "imu.h"
#include "i2c_interface.hpp"

// =================================================================
Imu::Imu() :  bus_fd(openBus("/dev/i2c-1", 0x68)) {

    writeRegisterByte(bus_fd, 0x6B, 0); // Reset
//    writeRegisterByte(bus_fd, 0x1C, 0x10); // Accelerometer range to 8g
//    writeRegisterByte(bus_fd, 0x1B, 0x10); // Gyto 2500 degrees/s
}


dlib::vector<double> Imu::readAccelerometer() {
    uint8_t data[6];
    readRegisterBlock(bus_fd, 0x3B, 6, data);

    double x = ((data[0] << 8) + (data[1] & 0xFF)) / 65536.0 * accelerometerFullScale;
    double y = ((data[2] << 8) + (data[3] & 0xFF)) / 65536.0 * accelerometerFullScale;
    double z = ((data[4] << 8) + (data[5] & 0xFF)) / 65536.0 * accelerometerFullScale;

    return dlib::vector<double>(x, y, z) + acceleratorOffset;

}
