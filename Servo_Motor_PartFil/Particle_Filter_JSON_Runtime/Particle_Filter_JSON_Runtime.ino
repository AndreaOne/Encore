#include <Arduino.h>
#include <ESP32Servo.h>
#include <FS.h>
#include <SPIFFS.h>
#include <math.h>

#include "BuiltInScoreBundle.h"
#include "ScoreBundle.h"

// Pins
#define MIC_PIN 34
#define SERVO_PIN 32
#define IN1 25
#define IN2 26
#define ENA 27

// Filesystem
const char* SCORE_BUNDLE_PATH = "/score_bundle.json";
const char* scoreBundleSource = SCORE_BUNDLE_PATH;

// Servo + motor
Servo pageServo;
int servoPosition = 0;
const int PWM_FREQ = 20000;
const int PWM_RESOLUTION = 8;
int motorSpeed = 200;
bool flipRunning = false;

// Audio capture
const int SAMPLE_RATE = 16000;
const int BUFFER_SIZE = 1024;
const float MIN_FREQ = 120.0f;
const float MAX_FREQ = 1400.0f;
const float RMS_THRESHOLD = 30.0f;
const int ADC_CENTER = 2048;
int16_t audioBuffer[BUFFER_SIZE];

// Particle filter
const int N_PARTICLES = 160;
const float PROCESS_SIGMA_MS = 20.0f;
const float RESAMPLE_SIGMA_MS = 8.0f;
const float PITCH_SIGMA = 16.0f;
float particles[N_PARTICLES];
float weights[N_PARTICLES];
float resampledParticles[N_PARTICLES];
float estimatedPositionMs = 0.0f;
int nextMarkerIndex = 0;
unsigned long lastLoopMs = 0;

// Score bundle
ScoreBundle scoreBundle;
bool scoreLoaded = false;
bool trackingLocked = false;
unsigned long lastStartupReportMs = 0;
unsigned long lastWaitingForPitchReportMs = 0;
const char* startupErrorMessage = nullptr;

constexpr unsigned long STARTUP_REPORT_INTERVAL_MS = 2000;
constexpr unsigned long WAITING_FOR_PITCH_REPORT_INTERVAL_MS = 750;

float randUniform() {
  return random(0, 10000) / 10000.0f;
}

float randNormal() {
  float u1 = randUniform();
  float u2 = randUniform();
  if (u1 < 1e-6f) {
    u1 = 1e-6f;
  }
  return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * PI * u2);
}

float clampFloat(float value, float lower, float upper) {
  if (value < lower) {
    return lower;
  }
  if (value > upper) {
    return upper;
  }
  return value;
}

float absFloat(float value) {
  return value < 0.0f ? -value : value;
}

float gaussian(float x, float mean, float sigma) {
  float delta = x - mean;
  return expf(-(delta * delta) / (2.0f * sigma * sigma));
}

float centsOff(float pitch, float reference) {
  return 1200.0f * logf(pitch / reference) / logf(2.0f);
}

float snapPitchToNearestSemitone(float pitch) {
  if (pitch <= 0.0f) {
    return -1.0f;
  }

  float midi = 69.0f + 12.0f * logf(pitch / 440.0f) / logf(2.0f);
  int nearestMidi = static_cast<int>(roundf(midi));
  float snapped = 440.0f * powf(2.0f, (nearestMidi - 69) / 12.0f);

  if (absFloat(centsOff(pitch, snapped)) > 35.0f) {
    return -1.0f;
  }

  return snapped;
}

void captureAudio(int16_t* buffer, int count, int sampleRate) {
  unsigned long periodMicros = 1000000UL / sampleRate;
  unsigned long startMicros = micros();

  for (int index = 0; index < count; index++) {
    while (micros() - startMicros < static_cast<unsigned long>(index) * periodMicros) {
    }

    int raw = analogRead(MIC_PIN);
    buffer[index] = raw - ADC_CENTER;
  }
}

float computeRMS(const int16_t* buffer, int count) {
  double sum = 0.0;

  for (int index = 0; index < count; index++) {
    sum += static_cast<double>(buffer[index]) * buffer[index];
  }

  return sqrt(sum / count);
}

float estimatePitch(const int16_t* buffer, int count, int sampleRate, float minFreq, float maxFreq) {
  static float centered[BUFFER_SIZE];
  float mean = 0.0f;

  for (int index = 0; index < count; index++) {
    mean += buffer[index];
  }
  mean /= count;

  for (int index = 0; index < count; index++) {
    centered[index] = buffer[index] - mean;
  }

  int minLag = sampleRate / maxFreq;
  int maxLag = sampleRate / minFreq;
  float bestCorrelation = -1.0f;
  int bestLag = -1;

  for (int lag = minLag; lag <= maxLag; lag++) {
    double correlation = 0.0;
    double energyA = 0.0;
    double energyB = 0.0;

    for (int index = 0; index < count - lag; index++) {
      float a = centered[index];
      float b = centered[index + lag];
      correlation += a * b;
      energyA += a * a;
      energyB += b * b;
    }

    if (energyA < 1e-9 || energyB < 1e-9) {
      continue;
    }

    float normalized = correlation / sqrt(energyA * energyB);
    if (normalized > bestCorrelation) {
      bestCorrelation = normalized;
      bestLag = lag;
    }
  }

  if (bestLag <= 0 || bestCorrelation < 0.35f) {
    return -1.0f;
  }

  return static_cast<float>(sampleRate) / bestLag;
}

void initParticles() {
  float duration = static_cast<float>(scoreBundle.durationMs);

  for (int index = 0; index < N_PARTICLES; index++) {
    particles[index] = clampFloat(0.0f + 120.0f * randNormal(), 0.0f, duration);
    weights[index] = 1.0f / N_PARTICLES;
  }

  estimatedPositionMs = 0.0f;
  nextMarkerIndex = 0;
}

void predictParticles(float dtMs) {
  float duration = static_cast<float>(scoreBundle.durationMs);

  for (int index = 0; index < N_PARTICLES; index++) {
    float stepMs = dtMs + PROCESS_SIGMA_MS * randNormal();
    if (stepMs < 0.0f) {
      stepMs = 0.0f;
    }
    particles[index] = clampFloat(particles[index] + stepMs, 0.0f, duration);
  }
}

float bestReferenceFrequencyAt(float positionMs, float observedPitch) {
  float bestFrequency = 0.0f;
  float bestScore = -1.0f;

  for (int index = 0; index < scoreBundle.noteCount; index++) {
    const NoteEvent& note = scoreBundle.notes[index];
    float noteStart = static_cast<float>(note.startMs);
    float noteEnd = noteStart + static_cast<float>(note.durationMs);

    if (noteStart > positionMs) {
      break;
    }

    if (positionMs < noteStart || positionMs >= noteEnd) {
      continue;
    }

    float score = observedPitch > 0.0f ? gaussian(observedPitch, note.frequencyHz, PITCH_SIGMA) : 1.0f;
    if (score > bestScore) {
      bestScore = score;
      bestFrequency = note.frequencyHz;
    }
  }

  return bestFrequency;
}

void updateWeights(float pitch) {
  float sum = 0.0f;

  for (int index = 0; index < N_PARTICLES; index++) {
    float expected = bestReferenceFrequencyAt(particles[index], pitch);
    float weight = expected > 0.0f ? gaussian(pitch, expected, PITCH_SIGMA) : 0.01f;

    weights[index] = weight + 1e-12f;
    sum += weights[index];
  }

  if (sum <= 0.0f) {
    for (int index = 0; index < N_PARTICLES; index++) {
      weights[index] = 1.0f / N_PARTICLES;
    }
    return;
  }

  for (int index = 0; index < N_PARTICLES; index++) {
    weights[index] /= sum;
  }
}

void resampleParticles() {
  float cumulative[N_PARTICLES];
  cumulative[0] = weights[0];

  for (int index = 1; index < N_PARTICLES; index++) {
    cumulative[index] = cumulative[index - 1] + weights[index];
  }

  float step = 1.0f / N_PARTICLES;
  float offset = randUniform() * step;

  int sourceIndex = 0;
  for (int index = 0; index < N_PARTICLES; index++) {
    float threshold = offset + index * step;
    while (sourceIndex < N_PARTICLES - 1 && cumulative[sourceIndex] < threshold) {
      sourceIndex++;
    }

    resampledParticles[index] = clampFloat(
      particles[sourceIndex] + RESAMPLE_SIGMA_MS * randNormal(),
      0.0f,
      static_cast<float>(scoreBundle.durationMs)
    );
  }

  for (int index = 0; index < N_PARTICLES; index++) {
    particles[index] = resampledParticles[index];
    weights[index] = 1.0f / N_PARTICLES;
  }
}

float estimateState() {
  float estimate = 0.0f;

  for (int index = 0; index < N_PARTICLES; index++) {
    estimate += particles[index] * weights[index];
  }

  return estimate;
}

void motorStart() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  ledcWrite(ENA, motorSpeed);
}

void motorStop() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  ledcWrite(ENA, 0);
}

void servoUp() {
  for (servoPosition = 0; servoPosition <= 180; servoPosition += 1) {
    pageServo.write(servoPosition);
    delay(15);
  }
}

void servoDown() {
  for (servoPosition = 180; servoPosition >= 0; servoPosition -= 1) {
    pageServo.write(servoPosition);
    delay(15);
  }
}

void flipPage() {
  if (flipRunning) {
    return;
  }

  flipRunning = true;
  Serial.println("FLIP START");

  motorStart();
  delay(2000);
  motorStop();

  servoUp();
  delay(500);
  servoDown();

  flipRunning = false;
  Serial.println("FLIP END");
}

void checkFlipMarkers(float estimatedMs) {
  if (flipRunning) {
    return;
  }

  while (nextMarkerIndex < scoreBundle.markerCount &&
         estimatedMs >= static_cast<float>(scoreBundle.markers[nextMarkerIndex].timeMs)) {
    const FlipMarker& marker = scoreBundle.markers[nextMarkerIndex];
    Serial.print("Flip marker crossed at measure ");
    Serial.print(marker.measure);
    Serial.print(" -> page ");
    Serial.println(marker.targetPage);

    nextMarkerIndex++;
    flipPage();
  }
}

void printBundleSummary() {
  Serial.print("Loaded score bundle from ");
  Serial.println(scoreBundleSource);
  Serial.print("Notes: ");
  Serial.println(scoreBundle.noteCount);
  Serial.print("Markers: ");
  Serial.println(scoreBundle.markerCount);
  Serial.print("Duration (ms): ");
  Serial.println(scoreBundle.durationMs);
}

void printWaitingForPitch(float rms, float rawPitch, float pitch) {
  Serial.print("Waiting for first pitch. RMS:");
  Serial.print(rms);
  Serial.print(" RawPitch:");
  Serial.print(rawPitch);
  Serial.print(" Pitch:");
  Serial.println(pitch);
}

void reportStartupBlocked() {
  unsigned long now = millis();
  if (startupErrorMessage == nullptr || now - lastStartupReportMs < STARTUP_REPORT_INTERVAL_MS) {
    return;
  }

  Serial.print("Startup blocked: ");
  Serial.println(startupErrorMessage);
  Serial.println("Serial Monitor should be 115200.");
  lastStartupReportMs = now;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("Encore booting...");

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENA, OUTPUT);
  ledcAttach(ENA, PWM_FREQ, PWM_RESOLUTION);

  pageServo.setPeriodHertz(50);
  pageServo.attach(SERVO_PIN, 500, 2400);
  servoPosition = 0;
  pageServo.write(0);

  analogReadResolution(12);
  analogSetPinAttenuation(MIC_PIN, ADC_11db);
  randomSeed(analogRead(MIC_PIN) + micros());

  if (SPIFFS.begin(false)) {
    scoreLoaded = loadScoreBundle(SPIFFS, SCORE_BUNDLE_PATH, scoreBundle);
    if (scoreLoaded) {
      scoreBundleSource = SCORE_BUNDLE_PATH;
      Serial.println("Loaded /score_bundle.json from SPIFFS.");
    } else {
      Serial.println("Failed to load /score_bundle.json");
      Serial.println("Falling back to the score bundle compiled into the firmware.");
    }
  } else {
    Serial.println("SPIFFS mount failed. Falling back to the built-in score bundle.");
  }

  if (!scoreLoaded) {
    scoreLoaded = loadScoreBundleFromJson(kBuiltInScoreBundleJson, scoreBundle);
    if (scoreLoaded) {
      scoreBundleSource = "built-in firmware bundle";
      Serial.println("Loaded built-in score bundle.");
    } else {
      startupErrorMessage = "Score loading failed. Rebuild with a valid built-in bundle or upload /score_bundle.json to SPIFFS.";
      Serial.println(startupErrorMessage);
      return;
    }
  }

  initParticles();
  printBundleSummary();
  lastLoopMs = millis();
  trackingLocked = false;
  startupErrorMessage = nullptr;
  Serial.println("Encore system ready. Waiting for first detected pitch.");
}

void loop() {
  if (!scoreLoaded) {
    reportStartupBlocked();
    delay(100);
    return;
  }

  captureAudio(audioBuffer, BUFFER_SIZE, SAMPLE_RATE);
  float rms = computeRMS(audioBuffer, BUFFER_SIZE);
  float rawPitch = -1.0f;
  float pitch = -1.0f;
  unsigned long now = millis();

  if (rms > RMS_THRESHOLD) {
    rawPitch = estimatePitch(audioBuffer, BUFFER_SIZE, SAMPLE_RATE, MIN_FREQ, MAX_FREQ);
    if (rawPitch > 0.0f) {
      pitch = snapPitchToNearestSemitone(rawPitch);
    }
  }

  if (!trackingLocked) {
    if (pitch <= 0.0f) {
      if (now - lastWaitingForPitchReportMs >= WAITING_FOR_PITCH_REPORT_INTERVAL_MS) {
        printWaitingForPitch(rms, rawPitch, pitch);
        lastWaitingForPitchReportMs = now;
      }
      delay(35);
      return;
    }

    trackingLocked = true;
    initParticles();
    estimatedPositionMs = 0.0f;
    nextMarkerIndex = 0;
    lastLoopMs = now;

    Serial.print("Pitch lock acquired at ");
    Serial.print(pitch);
    Serial.println(" Hz. Tracking started.");
  }

  float dtMs = static_cast<float>(now - lastLoopMs);
  lastLoopMs = now;
  predictParticles(dtMs);

  if (pitch > 0.0f) {
    updateWeights(pitch);
    estimatedPositionMs = estimateState();
    resampleParticles();
  } else {
    estimatedPositionMs = estimateState();
  }

  estimatedPositionMs = clampFloat(estimatedPositionMs, 0.0f, static_cast<float>(scoreBundle.durationMs));
  float expectedPitch = bestReferenceFrequencyAt(estimatedPositionMs, pitch);

  checkFlipMarkers(estimatedPositionMs);

  Serial.print("RMS:");
  Serial.print(rms);
  Serial.print(" RawPitch:");
  Serial.print(rawPitch);
  Serial.print(" Pitch:");
  Serial.print(pitch);
  Serial.print(" RefPitch:");
  Serial.print(expectedPitch);
  Serial.print(" PosMs:");
  Serial.print(estimatedPositionMs);
  Serial.print(" NextMarker:");
  Serial.print(nextMarkerIndex);
  if (nextMarkerIndex >= 0 && nextMarkerIndex < scoreBundle.markerCount) {
    float nextMarkerMs = static_cast<float>(scoreBundle.markers[nextMarkerIndex].timeMs);
    Serial.print(" MarkerMs:");
    Serial.print(nextMarkerMs);
    Serial.print(" UntilMarker:");
    Serial.print(nextMarkerMs - estimatedPositionMs);
  } else {
    Serial.print(" MarkerMs:none");
  }
  Serial.println();

  delay(35);
}
