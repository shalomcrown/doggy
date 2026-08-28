#ifndef IMU_H
#define IMU_H

#include "dog_status.h"

class Imu
{

public:
    Imu();
    int bus_fd;

    Vec3 gyroOffset {};
    Vec3 acceleratorOffset {};

    double accelerometerSensitivityPerBit {16384.0};
    double gyroSensitivityPerBit{131.0};

    Vec3 readGyro();
    Vec3 readAccelerometer();
    double readTemperature();
    bool isOpen() const;

};

#endif // IMU_H
