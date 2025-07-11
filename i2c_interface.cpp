#include <sys/ioctl.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <system_error>

extern "C" {
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <i2c/smbus.h>
}


#include "i2c_interface.hpp"

// Thanks to: https://github.com/barulicm/PiPCA9685

// =============================================================================

int openBus(const std::string& device, const uint8_t address) {
    int bus_fd = open(device.c_str(), O_RDWR);

    if (bus_fd < 0) {
      throw std::system_error(errno, std::system_category(), "Could not open i2c bus.");
    }

    if (ioctl(bus_fd, I2C_SLAVE, address) < 0) {
      throw std::system_error(errno, std::system_category(), "Could not set peripheral address.");
    }

    return bus_fd;
}

// =============================================================================

void writeRegisterByte(const int bus_fd, const uint8_t register_address, const uint8_t value) {
  i2c_smbus_data data;
  data.byte = value;

//  const auto err = i2c_smbus_write_byte_data(bus_fd, register_address, value);
  const auto err = i2c_smbus_access(bus_fd, I2C_SMBUS_WRITE, register_address, I2C_SMBUS_BYTE_DATA, &data);

  if (err) {
    const auto msg = "Could not write value (" + std::to_string(value) + ") to register " + std::to_string(register_address);
    throw std::system_error(errno, std::system_category(), msg);
  }
}

// =============================================================================

uint8_t readRegisterByte(const int bus_fd, const uint8_t register_address) {
  i2c_smbus_data data;

  const auto err = i2c_smbus_access(bus_fd, I2C_SMBUS_READ, register_address, I2C_SMBUS_BYTE_DATA, &data);

  if (err) {
    const auto msg = "Could not read value at register " + std::to_string(register_address);
    throw std::system_error(-err, std::system_category(), msg);
  }

  return data.byte & 0xFF;
}

// =============================================================================

void closeBus(int bus_fd) {
    close(bus_fd);
}
