#pragma once

#include <Arduino.h>
#include <ESP32Servo.h>

struct MotionControlConfig {
  int servoPin = 32;
  int motorIn1Pin = 25;
  int motorIn2Pin = 26;
  int motorEnablePin = 27;
  int motorPwm = 75;  // motor power
  int motorPwmback = 72;
  int motorRunMs = 270;  // time motor runs for
  int motorbackwards = 300;
  // notes
    // I think it will work moving forward if the first page turns well, because it gives some lift to the next pages
    // WORKS: motorPwm = 75, motorPwmback = 72, motorRunMs = 270, motorbackwards = 300
  int motorToServoDelayMs = 0;  // time between motor starting and servo starting
  int motorAfterServoStartMs = 0;  // time motor runs after servo starts
  int servoHomeAngle = 175; // rest angle
  int servoRestAngle = 175; // also rest angle
  int servoLiftAngle = 35; // far angle
  int servoThrowSettleMs = 120;
  int servoHoldMs = 1000;  // delay at the end of the half turn
  int servoReturnSettleMs = 120;
  int servoPeriodHz = 50;
  int servoPulseMinUs = 500;
  int servoPulseMaxUs = 2400;
};

class MotionControl {
 public:
  bool begin(const MotionControlConfig& config);
  void motorStart();
  void motorStartback();
  void motorStop();
  void servoUp();
  void flipPage();
  bool isFlipRunning() const;

 private:
  MotionControlConfig config_;
  Servo pageServo_;
  int servoPosition_ = 0;
  bool flipRunning_ = false;
};
