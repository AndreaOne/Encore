#pragma once

#include "WebScoreClient.h"

// Fill these in if you want the ESP32 to download the score JSON at boot.
// Leave downloadOnBoot = false to keep using the local SPIFFS score file.
static const WebScoreSettings kWebScoreSettings = {
  "",     // wifiSsid
  "",     // wifiPassword
  "",     // scoreJsonUrl
  "",     // authHeaderName
  "",     // authHeaderValue
  false   // downloadOnBoot
};
