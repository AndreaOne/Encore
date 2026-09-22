#include <Arduino.h>

// Select which wiring layout to test.
// 0 = ESP32 driver used in this repo: IN1/IN2/ENA on 25/26/27
// 1 = Arduino Motor Shield style: DIRECTION/PWM/BRAKE on 12/3/9
// Uncomment one of the lines below to force a layout.
// #define MOTOR_TEST_LAYOUT 0
// #define MOTOR_TEST_LAYOUT 1
#ifndef MOTOR_TEST_LAYOUT
  #if defined(ARDUINO_ARCH_ESP32)
    #define MOTOR_TEST_LAYOUT 0
  #else
    #define MOTOR_TEST_LAYOUT 1
  #endif
#endif

constexpr unsigned long RUN_TIME_MS = 2000;
constexpr unsigned long STOP_TIME_MS = 1200;

#if MOTOR_TEST_LAYOUT == 0
constexpr int MOTOR_IN1_PIN = 25;
constexpr int MOTOR_IN2_PIN = 26;
constexpr int MOTOR_ENABLE_PIN = 27;
constexpr int MOTOR_PWM_FREQ = 20000;
constexpr int MOTOR_PWM_RESOLUTION = 8;
constexpr int TEST_DUTY = 200;
const int rampDuties[] = {80, 120, 160, 200, 240};
#elif MOTOR_TEST_LAYOUT == 1
constexpr int DIRECTION_PIN = 12;
constexpr int PWM_PIN = 3;
constexpr int BRAKE_PIN = 9;
constexpr int TEST_DUTY = 110;
const int rampDuties[] = {60, 90, 120, 150, 180};
#else
  #error Unsupported MOTOR_TEST_LAYOUT value
#endif

constexpr size_t RAMP_STEP_COUNT = sizeof(rampDuties) / sizeof(rampDuties[0]);

void stopMotor() {
#if MOTOR_TEST_LAYOUT == 0
  digitalWrite(MOTOR_IN1_PIN, LOW);
  digitalWrite(MOTOR_IN2_PIN, LOW);
  ledcWrite(MOTOR_ENABLE_PIN, 0);
#else
  analogWrite(PWM_PIN, 0);
  digitalWrite(BRAKE_PIN, HIGH);
#endif
}

void driveDirectionA(int duty) {
#if MOTOR_TEST_LAYOUT == 0
  digitalWrite(MOTOR_IN1_PIN, LOW);
  digitalWrite(MOTOR_IN2_PIN, HIGH);
  ledcWrite(MOTOR_ENABLE_PIN, duty);
#else
  digitalWrite(DIRECTION_PIN, LOW);
  digitalWrite(BRAKE_PIN, LOW);
  analogWrite(PWM_PIN, duty);
#endif
}

void driveDirectionB(int duty) {
#if MOTOR_TEST_LAYOUT == 0
  digitalWrite(MOTOR_IN1_PIN, HIGH);
  digitalWrite(MOTOR_IN2_PIN, LOW);
  ledcWrite(MOTOR_ENABLE_PIN, duty);
#else
  digitalWrite(DIRECTION_PIN, HIGH);
  digitalWrite(BRAKE_PIN, LOW);
  analogWrite(PWM_PIN, duty);
#endif
}

void printPins() {
#if MOTOR_TEST_LAYOUT == 0
  Serial.println("Layout: ESP32 IN1/IN2/ENA");
  Serial.print("IN1 pin: ");
  Serial.println(MOTOR_IN1_PIN);
  Serial.print("IN2 pin: ");
  Serial.println(MOTOR_IN2_PIN);
  Serial.print("ENA pin: ");
  Serial.println(MOTOR_ENABLE_PIN);
#else
  Serial.println("Layout: Arduino DIRECTION/PWM/BRAKE");
  Serial.print("Direction pin: ");
  Serial.println(DIRECTION_PIN);
  Serial.print("PWM pin: ");
  Serial.println(PWM_PIN);
  Serial.print("Brake pin: ");
  Serial.println(BRAKE_PIN);
#endif
}

void printDirectionState(const char* label, bool directionA, int duty) {
  Serial.println();
  Serial.print(label);
  Serial.println(":");

#if MOTOR_TEST_LAYOUT == 0
  Serial.print("  IN1 = ");
  Serial.println(directionA ? "LOW" : "HIGH");
  Serial.print("  IN2 = ");
  Serial.println(directionA ? "HIGH" : "LOW");
  Serial.print("  ENA PWM duty = ");
  Serial.println(duty);
#else
  Serial.print("  DIRECTION = ");
  Serial.println(directionA ? "LOW" : "HIGH");
  Serial.println("  BRAKE = LOW");
  Serial.print("  PWM duty = ");
  Serial.println(duty);
#endif

  Serial.println("  Expect the motor to spin now.");
}

void runDirectionStep(const char* label, bool directionA, int duty) {
  printDirectionState(label, directionA, duty);

  if (directionA) {
    driveDirectionA(duty);
  } else {
    driveDirectionB(duty);
  }

  delay(RUN_TIME_MS);
  Serial.println("  Stopping motor.");
  stopMotor();
  delay(STOP_TIME_MS);
}

void runRampTest() {
  Serial.println();
  Serial.println("PWM ramp test:");
  Serial.println("  Expect the motor speed to increase step by step.");

  for (size_t i = 0; i < RAMP_STEP_COUNT; ++i) {
    const int duty = rampDuties[i];
    Serial.print("  Duty = ");
    Serial.println(duty);
    driveDirectionA(duty);
    delay(900);
  }

  Serial.println("  Ramp finished. Stopping motor.");
  stopMotor();
  delay(STOP_TIME_MS);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Motor pin test booting...");
  Serial.println("Open the Serial Monitor at 115200 baud.");
  printPins();

#if MOTOR_TEST_LAYOUT == 0
  pinMode(MOTOR_IN1_PIN, OUTPUT);
  pinMode(MOTOR_IN2_PIN, OUTPUT);
  pinMode(MOTOR_ENABLE_PIN, OUTPUT);
  ledcAttach(MOTOR_ENABLE_PIN, MOTOR_PWM_FREQ, MOTOR_PWM_RESOLUTION);
#else
  pinMode(DIRECTION_PIN, OUTPUT);
  pinMode(PWM_PIN, OUTPUT);
  pinMode(BRAKE_PIN, OUTPUT);
#endif

  stopMotor();
  Serial.println("The sketch will test both directions, then run a PWM ramp.");
  Serial.println("If direction is reversed, swap the two direction pins in your wiring or code.");
}

void loop() {
  runDirectionStep("Test 1 - Direction A", true, TEST_DUTY);
  runDirectionStep("Test 2 - Direction B", false, TEST_DUTY);
  runRampTest();

  Serial.println();
  Serial.println("Sequence complete. Repeating in 3 seconds.");
  delay(3000);
}
