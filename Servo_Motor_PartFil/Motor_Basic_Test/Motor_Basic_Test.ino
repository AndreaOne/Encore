#include <Arduino.h>

constexpr int IN1 = 25;
constexpr int IN2 = 26;
constexpr int ENA = 27;

constexpr int PWM_FREQ = 20000;
constexpr int PWM_RESOLUTION = 8;
constexpr int MOTOR_SPEED = 200;

void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENA, OUTPUT);

  ledcAttach(ENA, PWM_FREQ, PWM_RESOLUTION);

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  ledcWrite(ENA, 0);
}

void loop() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  ledcWrite(ENA, MOTOR_SPEED);
  delay(3000);

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  ledcWrite(ENA, 0);
  delay(2000);
}
