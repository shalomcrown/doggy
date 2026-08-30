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

inline Vec3 operator+(const Vec3 &a, const Vec3 &b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

// ================================================================================

class ImuReading {
public:
    bool ok = false;
    double temperature_c = 0.0;
    Vec3 accel;
    Vec3 gyro;
};

// ================================================================================

class BatteryReading {
public:
    bool ok = false;
    double voltage_v = 0.0;
};

// ================================================================================

struct ServoSnapshot {
    int id;
    std::string name;
    double angle;
    int pwm;
};

// ================================================================================

class DogStatus {
public:
    std::vector<DogError> errors;
    ImuReading imu;
    BatteryReading battery;
    std::vector<ServoSnapshot> servos;
};

#endif
