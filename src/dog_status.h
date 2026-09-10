#ifndef DOG_STATUS_H
#define DOG_STATUS_H

// Hardware IMU math (not a wire model). API Vec3 is doggy.v1.Vec3.

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

#endif
