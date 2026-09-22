#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <math.h>

#include "AudioInput.h"
#include "BuiltInScoreBundle.h"
#include "MotionControl.h"
#include "ParticleFilter.h"
#include "ParticleFilterConfig.h"
#include "ReferenceScore.h"
#include "WebScoreClient.h"
#include "reference_notes.h"
#include "web_score_settings.h"

namespace {

const char* kParticleFilterConfigPath = "/particle_filter_config.yaml";
const char* kScoreBundlePath = "/score_bundle.json";
constexpr unsigned long kStartupReportIntervalMs = 2000;
constexpr unsigned long kWaitingForPitchReportIntervalMs = 750;
constexpr unsigned long kStartupPitchWindowFloorMs = 250;
constexpr unsigned long kStartupPitchWindowCapMs = 1200;
constexpr float kStartupPitchToleranceCents = 60.0f;

AudioInput audioInput;
MotionControl motionControl;
ReferenceScore referenceScore;
ParticleFilter particleFilter;
ParticleFilterConfig particleFilterConfig;
WebScoreClient webScoreClient;

bool systemReady = false;
bool trackingLocked = false;
int nextFlipMarkerIndex = 0;
unsigned long lastLoopMs = 0;
unsigned long lastStartupReportMs = 0;
unsigned long lastWaitingForPitchReportMs = 0;
const char* startupErrorMessage = nullptr;

void printScoreSummary() {
  Serial.print("Score loaded. Notes: ");
  Serial.print(referenceScore.noteCount);
  Serial.print(" Markers: ");
  Serial.print(referenceScore.markerCount);
  Serial.print(" Duration(ms): ");
  Serial.println(referenceScore.durationMs);
}

void tryDownloadScoreBundle() {
  if (!kWebScoreSettings.downloadOnBoot) {
    Serial.println("Web download disabled. Using local SPIFFS score file if available.");
    return;
  }

  if (webScoreClient.downloadScoreJson(kWebScoreSettings, SPIFFS, kScoreBundlePath)) {
    Serial.println("Downloaded score bundle from website.");
  } else {
    Serial.println("Website score download skipped or failed. Falling back to local SPIFFS file.");
  }
}

bool loadRuntimeFiles() {
  bool spiffsMounted = SPIFFS.begin(false);
  if (spiffsMounted) {
    if (loadParticleFilterConfig(SPIFFS, kParticleFilterConfigPath, particleFilterConfig)) {
      Serial.println("Particle filter YAML loaded from SPIFFS.");
    } else {
      Serial.println("Particle filter YAML missing. Using built-in defaults.");
    }

    tryDownloadScoreBundle();
  } else {
    Serial.println("SPIFFS mount failed.");
  }

  if (loadReferenceScoreFromJson(kBuiltInScoreBundleJson, referenceScore)) {
    Serial.println("Loaded built-in score bundle.");
    return true;
  }

  Serial.println("Built-in score bundle parse failed.");

  if (spiffsMounted && loadReferenceScore(SPIFFS, kScoreBundlePath, referenceScore)) {
    Serial.println("Loaded /score_bundle.json from SPIFFS.");
    return true;
  }

  if (spiffsMounted) {
    Serial.println("Failed to load /score_bundle.json from SPIFFS.");
  }

  return false;
}

void printLoopDebug(
  const AudioObservation& observation,
  float trackedPitchHz,
  float estimatedMs,
  float expectedPitchHz
) {
  Serial.print("RMS:");
  Serial.print(observation.rms);
  Serial.print(" Gate:");
  Serial.print(observation.passedRmsGate ? 1 : 0);
  Serial.print(" Corr:");
  Serial.print(observation.pitchCorrelation);
  Serial.print(" RawPitch:");
  Serial.print(observation.rawPitchHz);
  Serial.print(" SnapPitch:");
  Serial.print(observation.snappedPitchHz);
  Serial.print(" Pitch:");
  Serial.print(trackedPitchHz);
  Serial.print(" RefPitch:");
  Serial.print(expectedPitchHz);
  Serial.print(" PosMs:");
  Serial.print(estimatedMs);
  Serial.print(" NextMarker:");
  Serial.print(nextFlipMarkerIndex);
  if (nextFlipMarkerIndex >= 0 && nextFlipMarkerIndex < referenceScore.markerCount) {
    float nextMarkerMs = static_cast<float>(referenceScore.markers[nextFlipMarkerIndex].timeMs);
    Serial.print(" MarkerMs:");
    Serial.print(nextMarkerMs);
    Serial.print(" UntilMarker:");
    Serial.print(nextMarkerMs - estimatedMs);
  } else {
    Serial.print(" MarkerMs:none");
  }
  Serial.println();
}

void printWaitingForPitch(const AudioObservation& observation, float trackedPitchHz) {
  Serial.print("Waiting for a score-start pitch. RMS:");
  Serial.print(observation.rms);
  Serial.print(" Th:");
  Serial.print(audioInput.activeRmsThreshold());
  Serial.print(" Gate:");
  Serial.print(observation.passedRmsGate ? 1 : 0);
  Serial.print(" Corr:");
  Serial.print(observation.pitchCorrelation);
  Serial.print(" RawPitch:");
  Serial.print(observation.rawPitchHz);
  Serial.print(" SnapPitch:");
  Serial.print(observation.snappedPitchHz);
  Serial.print(" Pitch:");
  Serial.println(trackedPitchHz);
}

float centsBetween(float leftHz, float rightHz) {
  if (leftHz <= 0.0f || rightHz <= 0.0f) {
    return 9999.0f;
  }

  return fabsf(1200.0f * logf(leftHz / rightHz) / logf(2.0f));
}

unsigned long startupPitchWindowEndMs() {
  if (referenceScore.noteCount <= 0) {
    return 0;
  }

  const ScoreNote& firstNote = referenceScore.notes[0];
  unsigned long windowEndMs = firstNote.startMs + firstNote.durationMs;
  if (windowEndMs < firstNote.startMs + kStartupPitchWindowFloorMs) {
    windowEndMs = firstNote.startMs + kStartupPitchWindowFloorMs;
  }
  if (windowEndMs > firstNote.startMs + kStartupPitchWindowCapMs) {
    windowEndMs = firstNote.startMs + kStartupPitchWindowCapMs;
  }

  return windowEndMs;
}

float snapPitchToScoreReference(float observedPitchHz) {
  if (observedPitchHz <= 0.0f || referenceScore.noteCount <= 0) {
    return -1.0f;
  }

  float bestPitchHz = -1.0f;
  float bestCents = 9999.0f;

  for (int index = 0; index < referenceScore.noteCount; index++) {
    float referencePitchHz = referenceScore.notes[index].frequencyHz;
    float cents = centsBetween(observedPitchHz, referencePitchHz);
    if (cents < bestCents) {
      bestCents = cents;
      bestPitchHz = referencePitchHz;
    }
  }

  if (bestCents > kStartupPitchToleranceCents) {
    return -1.0f;
  }

  return bestPitchHz;
}

bool pitchMatchesScoreStart(float observedPitchHz) {
  if (observedPitchHz <= 0.0f || referenceScore.noteCount <= 0) {
    return false;
  }

  unsigned long windowEndMs = startupPitchWindowEndMs();
  for (int index = 0; index < referenceScore.noteCount; index++) {
    const ScoreNote& note = referenceScore.notes[index];
    if (note.startMs > windowEndMs) {
      break;
    }

    if (centsBetween(observedPitchHz, note.frequencyHz) <= kStartupPitchToleranceCents) {
      return true;
    }
  }

  return false;
}

bool shouldBeginTracking(float observedPitchHz) {
  return pitchMatchesScoreStart(observedPitchHz);
}

float pitchForScoreTracking(const AudioObservation& observation) {
  if (observation.snappedPitchHz > 0.0f) {
    return observation.snappedPitchHz;
  }

  return -1.0f;
}

void reportStartupBlocked() {
  unsigned long now = millis();
  if (startupErrorMessage == nullptr || now - lastStartupReportMs < kStartupReportIntervalMs) {
    return;
  }

  Serial.print("Startup blocked: ");
  Serial.println(startupErrorMessage);
  Serial.println("Serial Monitor should be 115200.");
  lastStartupReportMs = now;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("Encore booting...");

  if (!audioInput.begin(AudioInputConfig())) {
    startupErrorMessage = "Audio input setup failed.";
    Serial.println(startupErrorMessage);
    return;
  }

  audioInput.calibrateNoiseFloor();
  Serial.print("Ambient RMS floor: ");
  Serial.println(audioInput.noiseFloorRms());
  Serial.print("Active RMS threshold: ");
  Serial.println(audioInput.activeRmsThreshold());
  if (audioInput.noiseCalibrationRejected()) {
    Serial.println("Startup noise calibration saw only loud samples. Using base RMS threshold.");
  }
  if (audioInput.noiseFloorRms() > 500.0f) {
    Serial.println("Warning: microphone noise floor is very high. Check mic wiring, gain, and ADC bias.");
  }

  if (!motionControl.begin(MotionControlConfig())) {
    startupErrorMessage = "Motion control setup failed.";
    Serial.println(startupErrorMessage);
    return;
  }

  randomSeed(static_cast<uint32_t>(analogRead(AudioInputConfig().micPin)) + micros());

  if (!loadRuntimeFiles()) {
    startupErrorMessage = "Score loading failed.";
    Serial.println(startupErrorMessage);
    return;
  }

  if (!particleFilter.begin(particleFilterConfig, &referenceScore)) {
    startupErrorMessage = "Particle filter setup failed.";
    Serial.println(startupErrorMessage);
    return;
  }

  particleFilter.initializeParticles();
  printScoreSummary();

  lastLoopMs = millis();
  systemReady = true;
  trackingLocked = false;
  startupErrorMessage = nullptr;
  Serial.println("Encore system ready. Waiting for a score-start pitch.");
}

void loop() {
  if (!systemReady) {
    reportStartupBlocked();
    delay(100);
    return;
  }

  AudioObservation observation = audioInput.captureObservation();
  float trackedPitchHz = snapPitchToScoreReference(pitchForScoreTracking(observation));
  unsigned long now = millis();

  if (!trackingLocked) {
    if (!shouldBeginTracking(trackedPitchHz)) {
      if (now - lastWaitingForPitchReportMs >= kWaitingForPitchReportIntervalMs) {
        printWaitingForPitch(observation, trackedPitchHz);
        lastWaitingForPitchReportMs = now;
      }
      delay(35);
      return;
    }

    trackingLocked = true;
    particleFilter.initializeParticles();
    particleFilter.setEstimatedPositionMs(0.0f);
    nextFlipMarkerIndex = 0;
    lastLoopMs = now;

    Serial.print("Pitch lock acquired at ");
    Serial.print(trackedPitchHz);
    Serial.println(" Hz. Tracking started.");
  }

  float estimatedMs = particleFilter.estimatedPositionMs();
  if (trackedPitchHz > 0.0f) {
    float deltaMs = static_cast<float>(now - lastLoopMs);
    lastLoopMs = now;
    particleFilter.predict(deltaMs);
    particleFilter.updateWeights(trackedPitchHz);
    estimatedMs = particleFilter.estimateState();
    if (particleFilter.shouldResample()) {
      particleFilter.resampleParticles();
      estimatedMs = particleFilter.estimateState();
    }
    particleFilter.setEstimatedPositionMs(estimatedMs);

    const ScoreFlipMarker* marker = advanceFlipMarker(referenceScore, estimatedMs, nextFlipMarkerIndex);
    if (marker != nullptr) {
      Serial.print("Flip marker crossed at measure ");
      Serial.print(marker->measure);
      Serial.print(" -> page ");
      Serial.println(marker->targetPage);
      motionControl.flipPage();
    }
  } else {
    lastLoopMs = now;
  }

  float expectedPitchHz = referencePitchAt(referenceScore, estimatedMs);
  printLoopDebug(observation, trackedPitchHz, estimatedMs, expectedPitchHz);

  delay(35);
}
