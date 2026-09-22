#include <Arduino.h>
#include <ESP32Servo.h>

constexpr int SERVO_PIN = 32;
constexpr int MOTOR_IN1_PIN = 25;
constexpr int MOTOR_IN2_PIN = 26;
constexpr int MOTOR_ENABLE_PIN = 27;

constexpr int MOTOR_PWM_FREQ = 20000;
constexpr int MOTOR_PWM_RESOLUTION = 8;
constexpr int MOTOR_PWM_DUTY = 220;
constexpr int SERVO_PULSE_MIN_US = 500;
constexpr int SERVO_PULSE_MAX_US = 2400;

Servo pageServo;

void motorStop() {
  digitalWrite(MOTOR_IN1_PIN, LOW);
  digitalWrite(MOTOR_IN2_PIN, LOW);
  ledcWrite(MOTOR_ENABLE_PIN, 0);
}

void motorForward(int duty) {
  digitalWrite(MOTOR_IN1_PIN, LOW);
  digitalWrite(MOTOR_IN2_PIN, HIGH);
  ledcWrite(MOTOR_ENABLE_PIN, duty);
}

void sweepServoOnce() {
  Serial.println("Servo -> 90");
  pageServo.write(90);
  delay(1200);

  Serial.println("Servo -> 180");
  pageServo.write(180);
  delay(1200);

  Serial.println("Servo -> 0");
  pageServo.write(0);
  delay(1200);
}

void runMotorOnce() {
  Serial.println("Motor ON");
  motorForward(MOTOR_PWM_DUTY);
  delay(2000);

  Serial.println("Motor OFF");
  motorStop();
  delay(1500);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("Motion hardware test booting...");

  pinMode(MOTOR_IN1_PIN, OUTPUT);
  pinMode(MOTOR_IN2_PIN, OUTPUT);
  pinMode(MOTOR_ENABLE_PIN, OUTPUT);
  ledcAttach(MOTOR_ENABLE_PIN, MOTOR_PWM_FREQ, MOTOR_PWM_RESOLUTION);
  motorStop();

  pageServo.setPeriodHertz(50);
  pageServo.attach(SERVO_PIN, SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US);
  pageServo.write(0);

  Serial.println("Expect: motor runs for 2 seconds, then servo moves.");
}

void loop() {
  runMotorOnce();
  sweepServoOnce();
}
