#include "WebScoreClient.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <cstring>

bool WebScoreClient::downloadScoreJson(
  const WebScoreSettings& settings,
  fs::FS& filesystem,
  const char* destinationPath
) {
  if (!settings.downloadOnBoot ||
      settings.scoreJsonUrl == nullptr ||
      strlen(settings.scoreJsonUrl) == 0) {
    return false;
  }

  if (!connectWifi(settings)) {
    return false;
  }

  HTTPClient http;
  int statusCode = -1;
  String payload;

  if (String(settings.scoreJsonUrl).startsWith("https://")) {
    WiFiClientSecure secureClient;
    secureClient.setInsecure();
    if (http.begin(secureClient, settings.scoreJsonUrl)) {
      if (settings.authHeaderName != nullptr && strlen(settings.authHeaderName) > 0) {
        http.addHeader(settings.authHeaderName, settings.authHeaderValue);
      }
      statusCode = http.GET();
      if (statusCode == HTTP_CODE_OK) {
        payload = http.getString();
      }
      http.end();
    }
  } else {
    WiFiClient client;
    if (http.begin(client, settings.scoreJsonUrl)) {
      if (settings.authHeaderName != nullptr && strlen(settings.authHeaderName) > 0) {
        http.addHeader(settings.authHeaderName, settings.authHeaderValue);
      }
      statusCode = http.GET();
      if (statusCode == HTTP_CODE_OK) {
        payload = http.getString();
      }
      http.end();
    }
  }

  disconnectWifi();

  if (statusCode != HTTP_CODE_OK || payload.length() == 0) {
    return false;
  }

  if (filesystem.exists(destinationPath)) {
    filesystem.remove(destinationPath);
  }

  File file = filesystem.open(destinationPath, FILE_WRITE);
  if (!file) {
    return false;
  }

  file.print(payload);
  file.close();
  return true;
}

bool WebScoreClient::connectWifi(const WebScoreSettings& settings) {
  if (settings.wifiSsid == nullptr || strlen(settings.wifiSsid) == 0) {
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(settings.wifiSsid, settings.wifiPassword);

  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < 15000UL) {
    delay(250);
  }

  return WiFi.status() == WL_CONNECTED;
}

void WebScoreClient::disconnectWifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}
