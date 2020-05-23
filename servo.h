#ifndef SERVO_H
#define SERVO_H

#include <Arduino.h>

class Servo 
{
    public:
        Servo(int pin_in);

        void set(int signal_in);
        int get();

        void write(int pulsewidth);
        static void write_all(Servo* servo[], const int num);

        static constexpr int MAX_THROW = 1800;
        static constexpr int MIN_THROW = 1200;

    private:
        void high();
        void low();

        int pin;
        int signal;

        unsigned long timer;
};

#endif 