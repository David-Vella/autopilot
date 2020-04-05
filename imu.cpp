#include "imu.h"

Mpu6050::Mpu6050() { }

void Mpu6050::begin() {
  Wire.beginTransmission(MPU6050_I2C_ADDRESS);
  Wire.write(MPU6050_POWER_MANAGEMENT_REGISTER);
  Wire.write(MPU_6050_POWER_ON);
  Wire.endTransmission();

  Wire.beginTransmission(MPU6050_I2C_ADDRESS);
  Wire.write(MPU6050_ACCELEROMETER_CONFIG_REGISTER);
  Wire.write(ACCELEROMETER_ACCURACY);
  Wire.endTransmission();

  Wire.beginTransmission(MPU6050_I2C_ADDRESS);
  Wire.write(MPU6050_GYRO_CONFIG_REGISTER);
  Wire.write(GYRO_ACCURACY);
  Wire.endTransmission();
}

void Mpu6050::fetch() {
  Wire.beginTransmission(MPU6050_I2C_ADDRESS);
  Wire.write(MPU6050_ACCEL_XOUT_H_REGISTER);
  Wire.endTransmission();

  Wire.requestFrom(MPU6050_I2C_ADDRESS, 14);
  while(Wire.available() < 14);
  
  for (int* ptr = data; ptr < data + 7; ++ptr) {
    *ptr = Wire.read()<<8|Wire.read();
  }
}

int Mpu6050::get(int val) {
  return data[val];
}

Imu::Imu(int led_pin) : status_led(led_pin), Mpu6050() {
  orientation.w = 0.7071;
  orientation.x = 0.7071;
  orientation.y = 0.0001;
  orientation.z = 0.0001;
}

void Imu::calibrate() {
  begin();

  pinMode(status_led, OUTPUT);

  int count = 0, rest = 0;
  int x_prev, y_prev, z_prev;
  
  timer = 0;
  while (rest < PRE_CALIBRATION_REST_TIMER) {
    while (micros() - timer < 4000);
    timer = micros();

    fetch();

    int x_diff = abs(get(ACCELX) - x_prev);
    int y_diff = abs(get(ACCELY) - y_prev);
    int z_diff = abs(get(ACCELZ) - z_prev);

    x_prev = get(ACCELX);
    y_prev = get(ACCELY);
    z_prev = get(ACCELZ);

    if (x_diff < MIN_ACCEL_DIFF 
     && y_diff < MIN_ACCEL_DIFF 
     && z_diff < MIN_ACCEL_DIFF)
      ++rest;
    else rest = 0;

    if (++count % 200 == 0) led_state = !led_state;
    digitalWrite(status_led, led_state);
  }

  x_zero = 0;
  y_zero = 0;
  z_zero = 0;

  double accel_x = 0;
  double accel_z = 0;
  double accel_y = 0;
  
  for (int i = 0; i < GYRO_CALIBRATION_READINGS; ++i) {
    while (micros() - timer < 4000);
    timer = micros();

    fetch();

    x_zero += get(GYROX);
    y_zero += get(GYROY);
    z_zero += get(GYROZ);

    accel_x += get(ACCELX);
    accel_y += get(ACCELY);
    accel_z += get(ACCELZ);

    if (++count % 25 == 0) led_state = !led_state;
    digitalWrite(status_led, led_state); 
  }
  x_zero /= GYRO_CALIBRATION_READINGS;
  y_zero /= GYRO_CALIBRATION_READINGS;
  z_zero /= GYRO_CALIBRATION_READINGS;

  accel_x /= GYRO_CALIBRATION_READINGS;
  accel_y /= GYRO_CALIBRATION_READINGS;
  accel_z /= GYRO_CALIBRATION_READINGS;

  accel_x -= ACCELX_LEVEL_READING;
  accel_y -= ACCELY_LEVEL_READING;
  accel_z -= ACCELZ_LEVEL_READING;

  #ifdef GRAVITY_ZERO
  Vector net_accel = { accel_x, accel_y, accel_z };
  double accel_angle_x = asin(accel_y / norm(net_accel)) * (1 / RADIANS_PER_DEGREE);
  double accel_angle_y = asin(accel_x / norm(net_accel)) * (1 / RADIANS_PER_DEGREE) * -1;

  Quaternion initial_roll = {
    cos(accel_angle_x * RADIANS_PER_DEGREE * 0.5), 
    sin(accel_angle_x * RADIANS_PER_DEGREE * 0.5),
    0.0001,
    0.0001
  };

  orientation = product(orientation, initial_roll);
  normalize(orientation);

  Quaternion initial_pitch = {
    cos(accel_angle_y * RADIANS_PER_DEGREE * 0.5), 
    0.0001, 
    sin(accel_angle_y * RADIANS_PER_DEGREE * 0.5), 
    0.0001 
  };

  orientation = product(orientation, initial_pitch);
  normalize(orientation);
  #endif
}

void Imu::calibrate_accel() {
  calibrate();
  Serial.println("Calibrating Accelerometer...");

  double x = 0;
  double y = 0;
  double z = 0;
  
  timer = 0;
  int count = 0;
  for (int i = 0; i < ACCEL_CALIBRATION_READINGS; ++i) {
    while (micros() - timer < 4000);
    timer = micros();

    fetch();

    x += get(ACCELX) / (float)ACCEL_CALIBRATION_READINGS;
    y += get(ACCELY) / (float)ACCEL_CALIBRATION_READINGS;
    z += get(ACCELZ) / (float)ACCEL_CALIBRATION_READINGS;

    if (++count % 25 == 0) led_state = !led_state;
    digitalWrite(status_led, led_state); 
  }

  Serial.print(" x: ");
  Serial.print(x);
  Serial.print(" y: ");
  Serial.print(y);
  Serial.print(" z: ");
  Serial.print(z - TICKS_PER_G);
  Serial.println();
}

void Imu::run() {
  fetch();

  static bool start = true;
  if (start) {
    start = false;
    timer = micros();
  }
  else {
    double t_delta = (micros() - timer) / 1000000.0; // seconds
    timer = micros();

    // create 3D vector of net angular velocity and finds its magnitude
    Vector w;
    w.x = (get(GYROX) - x_zero) * TICKS_PER_DEGREE * RADIANS_PER_DEGREE;
    w.y = (get(GYROY) - y_zero) * TICKS_PER_DEGREE * RADIANS_PER_DEGREE;
    w.z = (get(GYROZ) - z_zero) * TICKS_PER_DEGREE * RADIANS_PER_DEGREE;
    double w_norm = norm(w);

    // transform angular rate into a unit quaternion representing the rotation
    Quaternion rotation;
    rotation.w = cos((t_delta * w_norm) / 2);
    rotation.x = (sin((t_delta * w_norm) / 2) * w.x) / w_norm;
    rotation.y = (sin((t_delta * w_norm) / 2) * w.y) / w_norm;
    rotation.z = (sin((t_delta * w_norm) / 2) * w.z) / w_norm;

    // apply rotation
    orientation = product(orientation, rotation);

    // normalize, noise reduction
    normalize(orientation);

    // calculate roll, pitch, and yaw angles
    x_angle = atan2(2 * orientation.x * orientation.w - 2 * orientation.y * orientation.z, 
              1 - 2 * orientation.x * orientation.x - 2 * orientation.z * orientation.z);

    y_angle = asin(2 * orientation.x * orientation.y + 2 * orientation.z * orientation.w);

    z_angle = atan2(2 * orientation.y * orientation.w - 2 * orientation.x * orientation.z, 
              1 - 2 * orientation.y * orientation.y - 2 * orientation.z * orientation.z);

    // convert roll, pitch, and yaw angles to degrees
    x_angle /= RADIANS_PER_DEGREE;
    y_angle /= RADIANS_PER_DEGREE;
    z_angle /= RADIANS_PER_DEGREE;

    // correct roll axis offset due to Mpu6050 orientation in aircraft
    if (x_angle < -90) x_angle += 270;
    else x_angle -= 90;

    #ifdef INVERT_ROLL_AXIS
    x_angle *= -1;
    #endif    
    #ifdef INVERT_PITCH_AXIS
    y_angle *= -1;
    #endif
    #ifdef INVERT_YAW_AXIS
    z_angle *= -1;
    #endif
  }
}

double Imu::roll() {
  return x_angle;
}

double Imu::pitch() {
  return y_angle;
}

double Imu::yaw() {
  return z_angle;
}

Imu::Quaternion Imu::product(const Quaternion &p, const Quaternion &q) {
  Quaternion result;
  
  result.w = p.w * q.w - p.x * q.x - p.y * q.y - p.z * q.z;
  result.x = p.w * q.x + p.x * q.w + p.y * q.z - p.z * q.y;
  result.y = p.w * q.y - p.x * q.z + p.y * q.w + p.z * q.x;
  result.z = p.w * q.z + p.x * q.y - p.y * q.x + p.z * q.w;

  return result;
}

double Imu::norm(const Quaternion &q) {
  return sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
}

void Imu::normalize(Quaternion &q) {
  double l = norm(q);
  q.w /= l;
  q.x /= l;
  q.y /= l;
  q.x /= l;
}

double Imu::norm(const Vector &v) {
  return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}