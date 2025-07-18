#ifndef I2C_INTERFACE_HPP
#define I2C_INTERFACE_HPP

#include <string>
#include <unistd.h>


int openBus(const std::string& device, const uint8_t address);
void writeRegisterByte(const int bus_fd, const uint8_t register_address, const uint8_t value);
uint8_t readRegisterByte(const int bus_fd, const uint8_t register_address);
void readRegisterBlock(const int bus_fd, const uint8_t register_address, uint8_t blockSize, uint8_t *blockData);
int i2c_rdwr_block(int fd, uint8_t reg, uint8_t read_write, uint8_t length, unsigned char* buffer);
void closeBus(int bus_fd);

#endif // I2C_INTERFACE_HPP
