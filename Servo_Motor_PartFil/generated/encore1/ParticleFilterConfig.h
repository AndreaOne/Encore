#pragma once

#include <Arduino.h>
#include <FS.h>

struct ParticleFilterConfig {
  int particleCount = 160;
  // 调参入口：初始化粒子的扩散范围。越大表示一开始撒得越开，越小表示更贴近乐谱开头。
  float initialSpreadMs = 80.0f;
  // 调参入口：stepMs 的随机噪声强度。你截图里说的 noise 主要就是调这里。
  float processSigmaMs = 20.0f;
  // 调参入口：重采样后额外加的噪声。太大会乱跳，太小会太容易塌到同一点。
  float resampleSigmaMs = 8.0f;
  // 调参入口：音高匹配容忍度。越小越严格，越大越容易接受不太准的音。
  float pitchSigmaHz = 16.0f;
  // 调参入口：stepMs 的基础下限。想让它每轮至少往前一点，就调这里。
  float minimumStepMs = 0.0f;
  float resampleThresholdRatio = 0.5f;
};

static inline bool applyParticleFilterConfigEntry(
  ParticleFilterConfig& config,
  const String& key,
  const String& value
) {
  if (key == "particle_count") {
    config.particleCount = value.toInt();
    return true;
  }
  if (key == "initial_spread_ms") {
    config.initialSpreadMs = value.toFloat();
    return true;
  }
  if (key == "process_sigma_ms") {
    config.processSigmaMs = value.toFloat();
    return true;
  }
  if (key == "resample_sigma_ms") {
    config.resampleSigmaMs = value.toFloat();
    return true;
  }
  if (key == "pitch_sigma_hz") {
    config.pitchSigmaHz = value.toFloat();
    return true;
  }
  if (key == "minimum_step_ms") {
    config.minimumStepMs = value.toFloat();
    return true;
  }
  if (key == "resample_threshold_ratio") {
    config.resampleThresholdRatio = value.toFloat();
    return true;
  }

  return false;
}

static inline bool loadParticleFilterConfig(fs::FS& filesystem, const char* path, ParticleFilterConfig& config) {
  File file = filesystem.open(path, FILE_READ);
  if (!file) {
    return false;
  }

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();

    if (line.length() == 0 || line.startsWith("#")) {
      continue;
    }

    int commentIndex = line.indexOf('#');
    if (commentIndex >= 0) {
      line = line.substring(0, commentIndex);
      line.trim();
    }

    int colonIndex = line.indexOf(':');
    if (colonIndex < 0) {
      continue;
    }

    String key = line.substring(0, colonIndex);
    String value = line.substring(colonIndex + 1);
    key.trim();
    value.trim();

    applyParticleFilterConfigEntry(config, key, value);
  }

  file.close();

  if (config.particleCount <= 0) {
    config.particleCount = 160;
  }

  if (config.initialSpreadMs < 0.0f) {
    config.initialSpreadMs = 0.0f;
  }

  if (config.processSigmaMs < 0.0f) {
    config.processSigmaMs = 0.0f;
  }

  if (config.resampleSigmaMs < 0.0f) {
    config.resampleSigmaMs = 0.0f;
  }

  if (config.pitchSigmaHz <= 0.0f) {
    config.pitchSigmaHz = 16.0f;
  }

  if (config.minimumStepMs < 0.0f) {
    config.minimumStepMs = 0.0f;
  }

  if (config.resampleThresholdRatio <= 0.0f || config.resampleThresholdRatio > 1.0f) {
    config.resampleThresholdRatio = 0.5f;
  }

  return true;
}
