#include "ParticleFilter.h"

#include <math.h>
#include <new>

// Construct an empty particle filter Buffers are allocated later in begin()
ParticleFilter::ParticleFilter() {
}

// clean up any particle buffers allocated once the object is gone
ParticleFilter::~ParticleFilter() {
  releaseBuffers();
}

// Attach the filter to a loaded score and allocate runtime buffers using the YAML config
bool ParticleFilter::begin(const ParticleFilterConfig& config, const ReferenceScore* score) {
  releaseBuffers();

  if (score == nullptr || score->noteCount <= 0 || config.particleCount <= 0) {
    return false;
  }

  config_ = config;
  score_ = score;

  particles_ = new (std::nothrow) float[config_.particleCount];
  weights_ = new (std::nothrow) float[config_.particleCount];
  resampledParticles_ = new (std::nothrow) float[config_.particleCount];
  cumulativeWeights_ = new (std::nothrow) float[config_.particleCount];

  if (particles_ == nullptr ||
      weights_ == nullptr ||
      resampledParticles_ == nullptr ||
      cumulativeWeights_ == nullptr) {
    releaseBuffers();
    return false;
  }

  estimatedPositionMs_ = 0.0f;
  return true;
}

// scatter particles near the beginning of the score before tracking starts
void ParticleFilter::initializeParticles() {
  if (score_ == nullptr || particles_ == nullptr || weights_ == nullptr) {
    return;
  }

  float durationMs = static_cast<float>(score_->durationMs);
  for (int index = 0; index < config_.particleCount; index++) {
    // 调参位置：初始化粒子噪声实际就在这里乘上 initialSpreadMs。
    // Start near the beginning of the score, but with some spread so the filter can recover.
    particles_[index] = clampFloat(
      config_.initialSpreadMs * randNormal(),
      0.0f,
      durationMs
    );
    weights_[index] = 1.0f / config_.particleCount;
  }

  estimatedPositionMs_ = 0.0f;
}

// Push every particle forward in time using elapsed loop time plus process noise
void ParticleFilter::predict(float deltaMs) {
  if (score_ == nullptr || particles_ == nullptr) {
    return;
  }

  float durationMs = static_cast<float>(score_->durationMs);
  for (int index = 0; index < config_.particleCount; index++) {
    // 调参位置：stepMs 的基础值是 deltaMs，随机噪声由 processSigmaMs 控制。
    // deltaMs is the real time elapsed since the last loop.
    // We add Gaussian noise so particles do not all move in lockstep
    float stepMs = deltaMs + config_.processSigmaMs * randNormal();
    // 调参位置：minimumStepMs 是 stepMs 的基础下限。
    // minimumStepMs acts like a lower bound: 0 means particles can pause but not move backward.
    if (stepMs < config_.minimumStepMs) {
      stepMs = config_.minimumStepMs;
    }

    particles_[index] = clampFloat(particles_[index] + stepMs, 0.0f, durationMs);
  }
}

// Reweight particles with a Bayesian update: posterior is proportional to prior times likelihood.
void ParticleFilter::updateWeights(float observedPitchHz) {
  if (score_ == nullptr || weights_ == nullptr) {
    return;
  }

  float sum = 0.0f;
  for (int index = 0; index < config_.particleCount; index++) {
    float expectedPitchHz = bestReferenceFrequencyAt(particles_[index], observedPitchHz);
    // This is the observation likelihood p(y_t | x_t).
    float likelihood = expectedPitchHz > 0.0f
      ? gaussian(observedPitchHz, expectedPitchHz, config_.pitchSigmaHz)
      : 0.01f;

    // Standard Bayesian particle-filter update:
    // posterior weight = prior weight * likelihood.
    weights_[index] *= (likelihood + 1e-12f);
    sum += weights_[index];
  }

  if (sum <= 0.0f) {
    for (int index = 0; index < config_.particleCount; index++) {
      weights_[index] = 1.0f / config_.particleCount;
    }
    return;
  }

  for (int index = 0; index < config_.particleCount; index++) {
    weights_[index] /= sum;
  }
}

// Estimate how many particles are still effectively contributing after weighting
float ParticleFilter::effectiveSampleSize() const {
  if (weights_ == nullptr || config_.particleCount <= 0) {
    return 0.0f;
  }

  float sumSquaredWeights = 0.0f;
  for (int index = 0; index < config_.particleCount; index++) {
    sumSquaredWeights += weights_[index] * weights_[index];
  }

  if (sumSquaredWeights <= 1e-12f) {
    return 0.0f;
  }

  // ESS = 1 / sum(w_i^2). Smaller effective sample size means fewer particles are effectively alive (providing useful info).
  return 1.0f / sumSquaredWeights;
}

// Decide whether the particle set has become degenerate enough to execute resampling function
bool ParticleFilter::shouldResample() const {
  if (weights_ == nullptr || config_.particleCount <= 0) {
    return false;
  }

  float threshold = config_.resampleThresholdRatio * config_.particleCount;
  return effectiveSampleSize() < threshold;
}

// Replace low value particles with copies of high-value ones, then add a little jitter.
void ParticleFilter::resampleParticles() {
  if (score_ == nullptr ||
      particles_ == nullptr ||
      weights_ == nullptr ||
      resampledParticles_ == nullptr ||
      cumulativeWeights_ == nullptr) {
    return;
  }

  float cumulativeWeight = 0.0f;
  for (int index = 0; index < config_.particleCount; index++) {
    cumulativeWeight += weights_[index];
    cumulativeWeights_[index] = cumulativeWeight;
  }

  float step = 1.0f / config_.particleCount;
  float offset = randUniform() * step;
  float durationMs = static_cast<float>(score_->durationMs);
  int sourceIndex = 0;

  // Systematic resampling only runs after the particle set has become too degenerate
  for (int index = 0; index < config_.particleCount; index++) {
    float threshold = offset + index * step;

    while (sourceIndex < config_.particleCount - 1 && cumulativeWeights_[sourceIndex] < threshold) {
      sourceIndex++;
    }

    // 调参位置：这里用 resampleSigmaMs 控制重采样之后再抖动多少。
    // After resampling, keep a small amount of jitter so the filter does not collapse too early.
    resampledParticles_[index] = clampFloat(
      particles_[sourceIndex] + config_.resampleSigmaMs * randNormal(),
      0.0f,
      durationMs
    );
  }

  for (int index = 0; index < config_.particleCount; index++) {
    particles_[index] = resampledParticles_[index];
    weights_[index] = 1.0f / config_.particleCount;
  }
}

// Return the current score position estimate as the weighted average of all particles.
float ParticleFilter::estimateState() const {
  if (particles_ == nullptr || weights_ == nullptr) {
    return 0.0f;
  }

  float estimateMs = 0.0f;
  for (int index = 0; index < config_.particleCount; index++) {
    // Weighted average of all particle positions.
    estimateMs += particles_[index] * weights_[index];
  }

  return estimateMs;
}

// Store a clamped public estimate so outside code can query a safe score position.
void ParticleFilter::setEstimatedPositionMs(float estimatedPositionMs) {
  if (score_ == nullptr) {
    estimatedPositionMs_ = estimatedPositionMs;
    return;
  }

  estimatedPositionMs_ = clampFloat(
    estimatedPositionMs,
    0.0f,
    static_cast<float>(score_->durationMs)
  ); //make sure 0<=i<=end of score
}

// Return the last clamped estimate of where we are in the score.
float ParticleFilter::estimatedPositionMs() const {
  return estimatedPositionMs_;
}

// Generate a uniform random value in the range [0, 1).
float ParticleFilter::randUniform() const {
  return random(0, 10000) / 10000.0f;
}

// Generate Gaussian noise for particle motion and resampling jitter.
float ParticleFilter::randNormal() const {
  float u1 = randUniform();
  float u2 = randUniform();
  if (u1 < 1e-6f) {
    u1 = 1e-6f;
  }

  return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * PI * u2);
}

// Keep a value inside a closed interval.
float ParticleFilter::clampFloat(float value, float lower, float upper) const {
  if (value < lower) {
    return lower;
  }
  if (value > upper) {
    return upper;
  }
  return value;
}

// Score how close x is to a Gaussian centered at mean with width sigma.
float ParticleFilter::gaussian(float x, float mean, float sigma) const {
  float delta = x - mean;
  return expf(-(delta * delta) / (2.0f * sigma * sigma));
}

// Look up the reference note active at a given score position, preferring the best pitch match.
float ParticleFilter::bestReferenceFrequencyAt(float positionMs, float observedPitchHz) const {
  if (score_ == nullptr) {
    return 0.0f;
  }

  float bestFrequencyHz = 0.0f;
  float bestScore = -1.0f;

  for (int index = 0; index < score_->noteCount; index++) {
    const ScoreNote& note = score_->notes[index];
    float noteStartMs = static_cast<float>(note.startMs);
    float noteEndMs = noteStartMs + static_cast<float>(note.durationMs);

    if (noteStartMs > positionMs) {
      break;
    }

    if (positionMs < noteStartMs || positionMs >= noteEndMs) {
      continue;
    }

    // If multiple notes overlap at the same time, prefer the one closest to the observed pitch.
    float score = observedPitchHz > 0.0f
      ? gaussian(observedPitchHz, note.frequencyHz, config_.pitchSigmaHz)
      : 1.0f;

    if (score > bestScore) {
      bestScore = score;
      bestFrequencyHz = note.frequencyHz;
    }
  }

  return bestFrequencyHz;
}

// Release dynamically allocated buffers so begin() can safely reinitialize the filter
void ParticleFilter::releaseBuffers() {
  delete[] particles_;
  delete[] weights_;
  delete[] resampledParticles_;
  delete[] cumulativeWeights_;

  particles_ = nullptr;
  weights_ = nullptr;
  resampledParticles_ = nullptr;
  cumulativeWeights_ = nullptr;
}
