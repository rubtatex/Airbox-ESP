#include "SerialInfo.h"
#include "Config.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

static const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    default: return "unknown";
  }
}

void SerialInfo::begin(NetworkManager& network, BleManager& ble) {
  network_ = &network;
  ble_ = &ble;
}

void SerialInfo::tick() {
  // A few bytes per call: loop() must never block (hardware watchdog).
  for (uint8_t n = 0; n < 32 && Serial.available() > 0; n++) {
    int c = Serial.read();
    if (c == '\n') {
      line_[len_] = '\0';
      handleLine();
      len_ = 0;
    } else if (c != '\r' && c >= 0) {
      if (len_ < sizeof(line_) - 1) line_[len_++] = static_cast<char>(c);
      else len_ = 0;  // too long to be our command: drop the line
    }
  }
}

void SerialInfo::handleLine() {
  if (strcmp(line_, "airbox.info") == 0) printInfo();
}

void SerialInfo::printInfo() {
  JsonDocument doc;
  doc["firmware"] = "airbox-esp";
  doc["version"] = FIRMWARE_VERSION;
  doc["uptimeMs"] = millis();
  doc["resetReason"] = resetReasonName(esp_reset_reason());
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running) doc["slot"] = running->label;

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  bool connected = network_->isConnected();
  wifi["connected"] = connected;
  wifi["mode"] = network_->isApMode() ? "ap" : "station";
  if (connected) {
    wifi["ssid"] = network_->getConnectedSsid();
    wifi["ip"] = network_->getIp();
    wifi["rssi"] = WiFi.RSSI();
  } else if (network_->isApMode()) {
    wifi["ssid"] = network_->getApSsid();
    wifi["ip"] = network_->getIp();
  }

  JsonObject ble = doc["ble"].to<JsonObject>();
  ble["advertising"] = ble_->isAdvertising();
  ble["connected"] = ble_->hasConnectedClient();

  String out;
  serializeJson(doc, out);
  Serial.print("AIRBOX ");
  Serial.println(out);
}
