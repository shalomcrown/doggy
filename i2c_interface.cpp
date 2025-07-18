#include <sys/ioctl.h>
#include <cstring>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <system_error>
#include <iostream>

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


// =================================================================

// See: https://stackoverflow.com/questions/55976683/read-a-block-of-data-from-a-specific-registerfifo-using-c-c-and-i2c-in-raspb

int i2c_rdwr_block(int fd, uint8_t reg, uint8_t read_write, uint8_t length, unsigned char* buffer)
{
    struct i2c_smbus_ioctl_data ioctl_data;
    union i2c_smbus_data smbus_data;

    int rv;

    if(length > I2C_SMBUS_BLOCK_MAX)
    {
        std::cerr << "Requested Length is greater than the maximum specified" << std::endl;
        return -1;
    }

    // First byte is always the size to write and to receive
    // https://github.com/torvalds/linux/blob/master/drivers/i2c/i2c-core-smbus.c
    // (See i2c_smbus_xfer_emulated CASE:I2C_SMBUS_I2C_BLOCK_DATA)
    smbus_data.block[0] = length;

    if ( read_write != I2C_SMBUS_READ )
    {
        for(int i = 0; i < length; i++)
        {
            smbus_data.block[i + 1] = buffer[i];
        }
    }


    ioctl_data.read_write = read_write;
    ioctl_data.command = reg;
    ioctl_data.size = I2C_SMBUS_I2C_BLOCK_DATA;
    ioctl_data.data = &smbus_data;

    rv = ioctl (fd, I2C_SMBUS, &ioctl_data);
    if (rv < 0)
    {
        std::cerr << "Accessing I2C Read/Write failed! Error is: " << strerror(errno) << std::endl;
        return rv;
    }

    if (read_write == I2C_SMBUS_READ)
    {
        for(int i = 0; i < length; i++)
        {
            // Skip the first byte, which is the length of the rest of the block.
            buffer[i] = smbus_data.block[i+1];
        }
    }

    return rv;
}



void readRegisterBlock(const int bus_fd, const uint8_t register_address, uint8_t blockSize, uint8_t *blockData) {

//    const auto err = i2c_smbus_read_i2c_block_data(bus_fd, register_address, blockSize, blockData);
    const auto err = i2c_rdwr_block(bus_fd, register_address, I2C_SMBUS_READ, blockSize, blockData);

    if (err) {
      const auto msg = "Could not read value at register " + std::to_string(register_address);
      throw std::system_error(-err, std::system_category(), msg);
    }

}

// =============================================================================

void closeBus(int bus_fd) {
    close(bus_fd);
}
