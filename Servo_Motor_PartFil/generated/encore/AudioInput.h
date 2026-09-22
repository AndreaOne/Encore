#pragma once

#include <Arduino.h>

struct AudioObservation {
  float rms = 0.0f;
  float rawPitchHz = -1.0f;
  float snappedPitchHz = -1.0f;
  float pitchCorrelation = -1.0f;
  bool passedRmsGate = false;
};

struct AudioInputConfig {
  int micPin = 34;
  int sampleRate = 16000;
  int bufferSize = 1024;
  // 调参入口：监听的最低频率。太高会漏掉低音，例如 D4 / E4。
  float minFreqHz = 250.0f;
  // 调参入口：监听的最高频率。太低会漏掉高音，太高会更容易吃到噪声。
  float maxFreqHz = 1800.0f;
  // 调参入口：音量门限。Pitch 总是 -1 时，这里通常值得先调低一点试试。
  float rmsThreshold = 30.0f;
  // 调参入口：开机静音校准采样次数。越大越稳，但启动更慢。
  int noiseCalibrationSamples = 8;
  // 调参入口：开机后先等麦克风前端稳定多久再开始测静音底噪。
  int noiseCalibrationWarmupMs = 250;
  // 调参入口：动态门限 = 环境噪声 * 倍数 和 环境噪声 + 余量 中较大者。
  float noiseFloorMultiplier = 1.4f;
  float noiseFloorMargin = 20.0f;
  // 调参入口：超过这个 RMS 的样本不当作“静音底噪”平均值，但会作为兜底记录。
  float noiseCalibrationMaxRms = 800.0f;
  // 调参入口：自相关相关性门限。越低越容易误把噪声当成音高。
  float correlationThreshold = 0.22f;
  // 调参入口：snap 到最近音名时允许偏差多少音分。越小越严格，越不容易误锁。
  float snapToleranceCents = 60.0f;
  int adcCenter = 2048;
};

class AudioInput {
 public:
  bool begin(const AudioInputConfig& config);
  AudioObservation captureObservation();
  void calibrateNoiseFloor();
  float noiseFloorRms() const;
  float activeRmsThreshold() const;
  bool noiseCalibrationRejected() const;

 private:
  static constexpr int kMaxBufferSize = 1024;

  void captureAudio(int16_t* buffer, int count);
  float computeRms(const int16_t* buffer, int count) const;
  float estimatePitch(const int16_t* buffer, int count, float* bestCorrelationOut) const;
  float snapPitchToNearestSemitone(float pitchHz) const;
  float absFloat(float value) const;
  float centsOff(float pitchHz, float referenceHz) const;
  void updateEffectiveRmsThreshold(float measuredNoiseFloorRms);

  AudioInputConfig config_;
  int16_t buffer_[kMaxBufferSize];
  float noiseFloorRms_ = 0.0f;
  float effectiveRmsThreshold_ = 0.0f;
  bool noiseCalibrationRejected_ = false;
};
