#pragma once

#include <Arduino.h>

#include "ParticleFilterConfig.h"
#include "ReferenceScore.h"

class ParticleFilter {
 public:
  ParticleFilter();
  ~ParticleFilter();

  bool begin(const ParticleFilterConfig& config, const ReferenceScore* score);
  void initializeParticles();
  void predict(float deltaMs);
  void updateWeights(float observedPitchHz);
  float effectiveSampleSize() const;
  bool shouldResample() const;
  void resampleParticles();
  float estimateState() const;
  void setEstimatedPositionMs(float estimatedPositionMs);
  float estimatedPositionMs() const;

 private:
  float randUniform() const;
  float randNormal() const;
  float clampFloat(float value, float lower, float upper) const;
  float gaussian(float x, float mean, float sigma) const;
  float bestReferenceFrequencyAt(float positionMs, float observedPitchHz) const;
  void releaseBuffers();

  ParticleFilterConfig config_;
  const ReferenceScore* score_ = nullptr;
  // Each particle stores a hypothesis for the current score position in milliseconds.
  float* particles_ = nullptr;
  // Weight for each particle after comparing observed pitch with reference pitch.
  float* weights_ = nullptr;
  float* resampledParticles_ = nullptr;
  float* cumulativeWeights_ = nullptr;
  // Smoothed public-facing estimate of where we are in the score timeline.
  float estimatedPositionMs_ = 0.0f;
};
