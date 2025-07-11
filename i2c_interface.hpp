#ifndef I2C_INTERFACE_HPP
#define I2C_INTERFACE_HPP

#include <string>
#include <unistd.h>


int openBus(const std::string& device, const uint8_t address);
void writeRegisterByte(const int bus_fd, const uint8_t register_address, const uint8_t value);
uint8_t readRegisterByte(const int bus_fd, const uint8_t register_address);
void closeBus(int bus_fd);

#endif // I2C_INTERFACE_HPP
