#include "AudioInput.h"

#include <math.h>

bool AudioInput::begin(const AudioInputConfig& config) {
  if (config.bufferSize <= 0 || config.bufferSize > kMaxBufferSize) {
    return false;
  }

  config_ = config;
  noiseFloorRms_ = 0.0f;
  effectiveRmsThreshold_ = config_.rmsThreshold;
  noiseCalibrationRejected_ = false;

  analogReadResolution(12);
  analogSetPinAttenuation(config_.micPin, ADC_11db);
  return true;
}

AudioObservation AudioInput::captureObservation() {
  AudioObservation observation;

  captureAudio(buffer_, config_.bufferSize);
  observation.rms = computeRms(buffer_, config_.bufferSize);
  observation.passedRmsGate = observation.rms > effectiveRmsThreshold_;

  if (!observation.passedRmsGate) {
    return observation;
  }

  observation.rawPitchHz = estimatePitch(buffer_, config_.bufferSize, &observation.pitchCorrelation);
  if (observation.rawPitchHz > 0.0f) {
    observation.snappedPitchHz = snapPitchToNearestSemitone(observation.rawPitchHz);
  }

  return observation;
}

void AudioInput::calibrateNoiseFloor() {
  if (config_.noiseCalibrationSamples <= 0) {
    noiseFloorRms_ = 0.0f;
    effectiveRmsThreshold_ = config_.rmsThreshold;
    noiseCalibrationRejected_ = false;
    return;
  }

  if (config_.noiseCalibrationWarmupMs > 0) {
    delay(config_.noiseCalibrationWarmupMs);
  }

  float quietFloorCandidate = -1.0f;
  float fallbackFloorCandidate = -1.0f;
  for (int sampleIndex = 0; sampleIndex < config_.noiseCalibrationSamples; sampleIndex++) {
    captureAudio(buffer_, config_.bufferSize);
    float rms = computeRms(buffer_, config_.bufferSize);

    if (fallbackFloorCandidate < 0.0f || rms < fallbackFloorCandidate) {
      fallbackFloorCandidate = rms;
    }
    if (rms <= config_.noiseCalibrationMaxRms &&
        (quietFloorCandidate < 0.0f || rms < quietFloorCandidate)) {
      quietFloorCandidate = rms;
    }

    delay(5);
  }

  if (quietFloorCandidate >= 0.0f) {
    noiseFloorRms_ = quietFloorCandidate;
    noiseCalibrationRejected_ = false;
  } else if (fallbackFloorCandidate >= 0.0f &&
             fallbackFloorCandidate <= config_.noiseCalibrationMaxRms) {
    noiseFloorRms_ = fallbackFloorCandidate;
    noiseCalibrationRejected_ = false;
  } else {
    noiseFloorRms_ = 0.0f;
    noiseCalibrationRejected_ = true;
  }

  updateEffectiveRmsThreshold(noiseFloorRms_);
}

void AudioInput::captureAudio(int16_t* buffer, int count) {
  unsigned long periodMicros = 1000000UL / static_cast<unsigned long>(config_.sampleRate);
  unsigned long startMicros = micros();

  for (int index = 0; index < count; index++) {
    while (micros() - startMicros < static_cast<unsigned long>(index) * periodMicros) {
    }

    int raw = analogRead(config_.micPin);
    buffer[index] = static_cast<int16_t>(raw - config_.adcCenter);
  }
}

float AudioInput::computeRms(const int16_t* buffer, int count) const {
  double sum = 0.0;

  for (int index = 0; index < count; index++) {
    sum += static_cast<double>(buffer[index]) * buffer[index];
  }

  return sqrt(sum / count);
}

float AudioInput::estimatePitch(const int16_t* buffer, int count, float* bestCorrelationOut) const {
  static float centered[kMaxBufferSize];
  float mean = 0.0f;

  for (int index = 0; index < count; index++) {
    mean += buffer[index];
  }
  mean /= count;

  for (int index = 0; index < count; index++) {
    centered[index] = buffer[index] - mean;
  }

  int minLag = config_.sampleRate / config_.maxFreqHz;
  int maxLag = config_.sampleRate / config_.minFreqHz;
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

  if (bestCorrelationOut != nullptr) {
    *bestCorrelationOut = bestCorrelation;
  }

  // 调参入口：相关性门限。越低越容易“听到”音高，但也更容易误判。
  if (bestLag <= 0 || bestCorrelation < config_.correlationThreshold) {
    return -1.0f;
  }

  return static_cast<float>(config_.sampleRate) / bestLag;
}

float AudioInput::snapPitchToNearestSemitone(float pitchHz) const {
  if (pitchHz <= 0.0f) {
    return -1.0f;
  }

  float midi = 69.0f + 12.0f * logf(pitchHz / 440.0f) / logf(2.0f);
  int nearestMidi = static_cast<int>(roundf(midi));
  float snapped = 440.0f * powf(2.0f, (nearestMidi - 69) / 12.0f);

  // 调参入口：snap 到最近音名时允许偏差多少音分。越大越宽松。
  if (absFloat(centsOff(pitchHz, snapped)) > config_.snapToleranceCents) {
    return -1.0f;
  }

  return snapped;
}

float AudioInput::absFloat(float value) const {
  return value < 0.0f ? -value : value;
}

float AudioInput::centsOff(float pitchHz, float referenceHz) const {
  return 1200.0f * logf(pitchHz / referenceHz) / logf(2.0f);
}

void AudioInput::updateEffectiveRmsThreshold(float measuredNoiseFloorRms) {
  float multiplied = measuredNoiseFloorRms * config_.noiseFloorMultiplier;
  float offset = measuredNoiseFloorRms + config_.noiseFloorMargin;

  effectiveRmsThreshold_ = multiplied > offset ? multiplied : offset;
  if (effectiveRmsThreshold_ < config_.rmsThreshold) {
    effectiveRmsThreshold_ = config_.rmsThreshold;
  }
}

float AudioInput::noiseFloorRms() const {
  return noiseFloorRms_;
}

float AudioInput::activeRmsThreshold() const {
  return effectiveRmsThreshold_;
}

bool AudioInput::noiseCalibrationRejected() const {
  return noiseCalibrationRejected_;
}
