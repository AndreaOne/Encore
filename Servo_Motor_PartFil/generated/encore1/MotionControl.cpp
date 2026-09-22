#include "MotionControl.h"

bool MotionControl::begin(const MotionControlConfig& config) {
  config_ = config;

  pinMode(config_.motorIn1Pin, OUTPUT);
  pinMode(config_.motorIn2Pin, OUTPUT);
  pinMode(config_.motorEnablePin, OUTPUT);

  analogWriteResolution(config_.motorEnablePin, 8);

  pageServo_.setPeriodHertz(config_.servoPeriodHz);
  pageServo_.attach(config_.servoPin, config_.servoPulseMinUs, config_.servoPulseMaxUs);
  servoPosition_ = config_.servoHomeAngle;
  pageServo_.write(servoPosition_);

  motorStop();
  return true;
}

void MotionControl::motorStart() {
  digitalWrite(config_.motorIn1Pin, LOW);
  digitalWrite(config_.motorIn2Pin, HIGH);
  analogWrite(config_.motorEnablePin, config_.motorPwm);
}

void MotionControl::motorStop() {
  digitalWrite(config_.motorIn1Pin, LOW);
  digitalWrite(config_.motorIn2Pin, LOW);
  analogWrite(config_.motorEnablePin, 0);
}

void MotionControl::servoUp() {
  int stepDegrees = config_.servoStepDegrees > 0 ? config_.servoStepDegrees : 1;
  int pressStepDegrees = config_.servoPressStepDegrees > 0 ? config_.servoPressStepDegrees : 1;
  int pressStartAngle = config_.servoLiftAngle + config_.servoPressMarginDegrees;
  if (pressStartAngle > config_.servoRestAngle) {
    pressStartAngle = config_.servoRestAngle;
  }

  while (servoPosition_ > pressStartAngle) {
    servoPosition_ -= stepDegrees;
    if (servoPosition_ < pressStartAngle) {
      servoPosition_ = pressStartAngle;
    }
    pageServo_.write(servoPosition_);
    delay(config_.servoRiseDelayMs);
  }

  while (servoPosition_ > config_.servoLiftAngle) {
    servoPosition_ -= pressStepDegrees;
    if (servoPosition_ < config_.servoLiftAngle) {
      servoPosition_ = config_.servoLiftAngle;
    }
    pageServo_.write(servoPosition_);
    delay(config_.servoPressDelayMs);
  }

  delay(config_.servoHoldMs);

  while (servoPosition_ < config_.servoRestAngle) {
    servoPosition_ += stepDegrees;
    if (servoPosition_ > config_.servoRestAngle) {
      servoPosition_ = config_.servoRestAngle;
    }
    pageServo_.write(servoPosition_);
    delay(config_.servoReturnDelayMs);
  }
}

void MotionControl::flipPage() {
  if (flipRunning_) {
    return;
  }

  flipRunning_ = true;
  Serial.println("FLIP START");

  motorStart();
  delay(config_.motorRunMs);
  motorStop();

  servoUp();

  flipRunning_ = false;
  Serial.println("FLIP END");
}

bool MotionControl::isFlipRunning() const {
  return flipRunning_;
}
