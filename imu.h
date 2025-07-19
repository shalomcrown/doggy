#ifndef IMU_H
#define IMU_H

#include <dlib/matrix.h>

class Imu
{

public:
    Imu();
    int bus_fd;

    dlib::vector<double> gyroOffset {0.0, 0.0, 0.0};
    dlib::vector<double> acceleratorOffset {0.0, 0.0, 0.0};

    double accelerometerSensitivityPerBit {16384.0};
    double gyroSensitivityPerBit{131.0};

    dlib::vector<double> readGyro();
    dlib::vector<double> readAccelerometer();
    double readTemperature();

};

#endif // IMU_H
