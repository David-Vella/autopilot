#include <Arduino.h>

#include "imu.h"
#include "ppm.h"
#include "servo.h"

#include "logging.h"

//#define CALIBRATE_ACCELEROMETER
//#define DISABLE_SERVOS

// units: degrees
#define MAX_ROLL_ANGLE 40
#define MAX_PITCH_ANGLE 40

// units microseconds
#define AUTOCLIMB_TRIM 75

// units: degrees
#define PID_ROLL_TRIM 2
#define PID_PITCH_TRIM -1

unsigned long timer = 0;

const int status_led = 13;

Imu imu(status_led);
ppmDecoder ppm;

const int THR = 0;
const int RTS = 1;
const int RBS = 2;
const int LTS = 3;
const int LBS = 4;
const int NUM_SERVOS = 5;
Servo* servo[NUM_SERVOS];

class PIDcontroller
{
  public:
  PIDcontroller(double p_in, double i_in, double d_in, double i_max_in) 
    : p(p_in), i(i_in), d(d_in), i_max(i_max_in) { }
  double calculate(double error)
  {
    static bool start = true;
    if (start)
    {
      timer = micros();
      start = false;
      return 1500;
    }
    else 
    {
      int t_delta = micros() - timer;
      timer = micros();

      double output = p * error;

      i_output += t_delta * error;
      if (i_output > i_max) i_output = i_max;
      if (i_output < -1 * i_max) i_output = -1 * i_max;
      output += i * i_output;

      output += d * ((error - prev_error) / t_delta) * 1000000;
      prev_error = error;

      return output;
    }  
  }
  private:
  double p;
  double i;
  double d;
  double i_max;
  double i_output;
  unsigned long timer;
  double prev_output;
  double prev_error;
};

PIDcontroller roll_pid(12, 0, 0, 1);
PIDcontroller pitch_pid(24, 0, 0.5, 1);

void setup() 
{
  pinMode(status_led, OUTPUT);

  #ifdef SERIAL_CONNECTION
  Serial.begin(9600);
  Serial.println("Serial connection");
  #endif

  #ifdef CALIBRATE_ACCELEROMETER
  imu.calibrate_accel();
  #else
  imu.calibrate();
  #endif

  assignPpmDecoderToPin(ppm, 2);

  servo[THR] = new Servo(4);
  servo[RTS] = new Servo(5);
  servo[RBS] = new Servo(6);
  servo[LTS] = new Servo(7);
  servo[LBS] = new Servo(8);
  
  digitalWrite(status_led, LOW);
  timer = micros();
}

void loop() 
{
  static int state = 0;
  static int fmode = 0;

  const int PASSTHRU = 0;
  const int AUTOLEVEL = 1;
  const int AUTOCLIMB = 2;

  static int arl_out, ele_out, rud_out;
  
  imu.run();

  if (state == 0)
  {
    ppm.sync();

    fmode = AUTOCLIMB;
    if (ppm.get(ppmDecoder::AUX) < 1700)
      fmode = AUTOLEVEL;
    if (ppm.get(ppmDecoder::AUX) < 1300)
      fmode = PASSTHRU;
  }
  if (state == 1)
  {
    double roll_target = (1500 - ppm.get(ppmDecoder::ARL)) * 0.04;
    double pitch_target = (1500 - ppm.get(ppmDecoder::ELE)) * 0.04;
    if (fmode == PASSTHRU)
    {
      arl_out = ppm.get(ppmDecoder::ARL);
      ele_out = ppm.get(ppmDecoder::ELE);
    }
    else
    {
      arl_out = roll_pid.calculate(imu.roll() - roll_target + PID_ROLL_TRIM) + 1500;
      ele_out = pitch_pid.calculate(imu.pitch() - pitch_target + PID_PITCH_TRIM) + 1500;
    }
    rud_out = ppm.get(ppmDecoder::RUD);
  }
  if (state == 2)
  {
    servo[THR]->set(ppm.get(ppmDecoder::THR));

    int trim = 0;
    if (fmode == AUTOCLIMB)
      trim = AUTOCLIMB_TRIM;

    // add definitions for max and min throw for each channel
    servo[RTS]->set(constrain(map_right_top(arl_out, ele_out, rud_out), 1200, 1800) + trim);
    servo[RBS]->set(constrain(map_right_bottom(arl_out, ele_out, rud_out), 1200, 1800) - trim);
    servo[LTS]->set(constrain(map_left_top(arl_out, ele_out, rud_out), 1200, 1800) - trim);
    servo[LBS]->set(constrain(map_left_bottom(arl_out, ele_out, rud_out), 1200, 1800) + trim);
  }
  if (state == 3)
  {
    #ifndef DISABLE_SERVOS
    Servo::write_all(servo, NUM_SERVOS);
    #endif 
  }

  if (state == 3) 
    state = 0;
  else
    ++state;

  #ifdef PRINT_LOOP_TIME
  int loop_time = micros() - timer;
  #endif

  #ifdef DO_LOGGING
  print_log()
  #endif

  if (micros() - timer > 5000) digitalWrite(status_led, HIGH);
  while (micros() - timer < 5000);
  timer = micros();
}
