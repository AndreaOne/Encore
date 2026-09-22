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

// CHANGED
void MotionControl::motorStartback() {
  digitalWrite(config_.motorIn1Pin, HIGH);
  digitalWrite(config_.motorIn2Pin, LOW);
  analogWrite(config_.motorEnablePin, config_.motorPwmback);
}

void MotionControl::motorStop() {
  digitalWrite(config_.motorIn1Pin, LOW);
  digitalWrite(config_.motorIn2Pin, LOW);
  analogWrite(config_.motorEnablePin, 0);
}

void MotionControl::servoUp() {
  servoPosition_ = config_.servoLiftAngle;
  pageServo_.write(servoPosition_);
  delay(config_.servoThrowSettleMs);

  // backwards
  delay(config_.motorbackwards);

  motorStartback();

  delay(config_.motorbackwards);

  motorStop();

  delay(config_.servoHoldMs);

  servoPosition_ = config_.servoRestAngle;
  pageServo_.write(servoPosition_);
  delay(config_.servoReturnSettleMs);
}

void MotionControl::flipPage() {
  if (flipRunning_) {
    return;
  }

  flipRunning_ = true;
  Serial.println("FLIP START");

  motorStart();
  delay(config_.motorRunMs);
  delay(config_.motorToServoDelayMs);

  unsigned long servoStartMs = millis();
  servoUp();
  unsigned long motorOverlapElapsedMs = millis() - servoStartMs;
  if (motorOverlapElapsedMs < static_cast<unsigned long>(config_.motorAfterServoStartMs)) {
    delay(static_cast<unsigned long>(config_.motorAfterServoStartMs) - motorOverlapElapsedMs);
  }
 
  motorStop();

  flipRunning_ = false;
  Serial.println("FLIP END");
}

bool MotionControl::isFlipRunning() const {
  return flipRunning_;
}
