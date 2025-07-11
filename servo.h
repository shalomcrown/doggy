#ifndef SERVO_H
#define SERVO_H



class Servo {
private:
    int bus_fd;
    double frequency;

public:

    void openBoard();
    void set_all_pwm(const uint16_t on, const uint16_t off);
    void set_pwm_freq(const double freq_hz);
    void set_pwm(const int channel, const uint16_t on, const uint16_t off);
    void set_pwm_ms(const int channel, const double ms);
};




#endif // SERVO_H
