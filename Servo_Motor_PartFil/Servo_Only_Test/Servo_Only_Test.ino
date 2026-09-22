#include <Arduino.h>
#include <ESP32Servo.h>

constexpr int SERVO_PIN = 32;
constexpr int SERVO_PULSE_MIN_US = 500;
constexpr int SERVO_PULSE_MAX_US = 2400;

Servo testServo;

void moveToPulse(int pulseUs, const char* label) {
  Serial.print("Servo pulse -> ");
  Serial.print(label);
  Serial.print(" (");
  Serial.print(pulseUs);
  Serial.println(" us)");
  testServo.writeMicroseconds(pulseUs);
  delay(2000);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Servo-only test booting...");

  testServo.setPeriodHertz(50);
  testServo.attach(SERVO_PIN, SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US);
  delay(1000);

  Serial.println("Expect: servo center, one side, other side, center.");
}

void loop() {
  moveToPulse(1500, "center");
  moveToPulse(1000, "side A");
  moveToPulse(2000, "side B");
  moveToPulse(1500, "center");
}
