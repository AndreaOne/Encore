#pragma once

#include <Arduino.h>
#include <ESP32Servo.h>

struct MotionControlConfig {
  int servoPin = 32;
  int motorIn1Pin = 25;
  int motorIn2Pin = 26;
  int motorEnablePin = 27;
  int motorPwm = 125;
  int motorRunMs = 315;
  int servoHomeAngle = 150;
  int servoRestAngle = 150;
  int servoLiftAngle = 10;
  int servoStepDegrees = 10;
  int servoRiseDelayMs = 2;
  int servoPressMarginDegrees = 20;
  int servoPressStepDegrees = 2;
  int servoPressDelayMs = 8;
  int servoHoldMs = 320;
  int servoReturnDelayMs = 1;
  int servoPeriodHz = 50;
  int servoPulseMinUs = 500;
  int servoPulseMaxUs = 2400;
};

class MotionControl {
 public:
  bool begin(const MotionControlConfig& config);
  void motorStart();
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
