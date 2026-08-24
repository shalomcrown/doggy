#ifndef DOG_STATUS_H
#define DOG_STATUS_H

#include <string>
#include <vector>

// ================================================================================

enum class DogErrorCode {
    i2c
};

// ================================================================================

class DogError {
public:
    DogErrorCode code;
    std::string message;
};

// ================================================================================

class Vec3 {
public:
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// ================================================================================

class ImuReading {
public:
    bool ok = false;
    double temperature_c = 0.0;
    Vec3 accel;
    Vec3 gyro;
};

// ================================================================================

class DogStatus {
public:
    std::vector<DogError> errors;
    ImuReading imu;
};

#endif
