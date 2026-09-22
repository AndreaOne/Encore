#pragma once

#include <Arduino.h>
#include <FS.h>

struct WebScoreSettings {
  const char* wifiSsid;
  const char* wifiPassword;
  const char* scoreJsonUrl;
  const char* authHeaderName;
  const char* authHeaderValue;
  bool downloadOnBoot;
};

class WebScoreClient {
 public:
  bool downloadScoreJson(const WebScoreSettings& settings, fs::FS& filesystem, const char* destinationPath);

 private:
  bool connectWifi(const WebScoreSettings& settings);
  void disconnectWifi();
};
