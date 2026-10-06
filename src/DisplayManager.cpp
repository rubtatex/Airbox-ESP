#include "DisplayManager.h"
#include "Config.h"
#include <qrcode.h>

namespace {
// QR version deliberately large compared to the actual content (~32 bytes
// for "WIFI:T:nopass;S:Airbox-XXXXXX;;"): the QRCode lib does not grow the
// buffer automatically if the text doesn't fit in the requested version
// — better a comfortable margin (buffer too small = memory overflow,
// not just a broken QR).
constexpr uint8_t kQrVersion = 6;
}  // namespace

void DisplayManager::begin() {
  // NOTE: U8g2 (HW_I2C mode) calls Wire.begin() internally WITHOUT arguments
  // on first access. On the XIAO ESP32-C3, the Arduino variant's default I2C
  // pins are already GPIO6/GPIO7 (= D4/D5), so it ends up on the right
  // pins. See CLAUDE.md if this firmware is ported to another board.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  Serial.println("[DisplayManager] scan I2C...");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[DisplayManager]   device found at 0x%02X\n", addr);
      found++;
    }
  }
  if (found == 0) {
    Serial.println(
        "[DisplayManager]   NO I2C device detected -> wiring/power problem.");
  }

  if (!u8g2_.begin()) {
    Serial.println("[DisplayManager] u8g2.begin() failed (screen not detected/miswired)");
    ready_ = false;
    return;
  }
  ready_ = true;
  u8g2_.setFont(u8g2_font_6x10_tf);
  u8g2_.clearBuffer();
  centerText(tr(Str::BootTitle, language_), 30);
  centerText(tr(Str::BootStarting, language_), 46);
  u8g2_.sendBuffer();
}

void DisplayManager::onShortPress() {
  if (safetyState_ == SystemState::Estop) return;  // ESTOP always takes priority
  if (peekPage_ == PeekPage::None) {
    peekPage_ = PeekPage::Network;
  } else if (peekPage_ == PeekPage::Network) {
    peekPage_ = PeekPage::ApiKey;
  } else {
    peekPage_ = PeekPage::None;
  }
  peekUntilMs_ = millis() + kPeekDurationMs;
}

void DisplayManager::showTemporaryText(const String& line1, const String& line2,
                                        uint32_t durationMs) {
  tempActive_ = true;
  tempLine1_ = line1;
  tempLine2_ = line2;
  tempUntilMs_ = millis() + durationMs;
}

void DisplayManager::setSafetyState(SystemState state, uint32_t rebreathRemainingMs,
                                     uint32_t rebreathCapMs, uint32_t airRemainingMs,
                                     uint32_t airCapMs) {
  safetyState_ = state;
  rebreathRemainingMs_ = rebreathRemainingMs;
  rebreathCapMs_ = rebreathCapMs > 0 ? rebreathCapMs : 1;
  airRemainingMs_ = airRemainingMs;
  airCapMs_ = airCapMs > 0 ? airCapMs : 1;
}

void DisplayManager::setRelayStates(const RelayLogicalState& air, const RelayLogicalState& rebreath,
                                     const RelayLogicalState& aroma) {
  air_ = air;
  rebreath_ = rebreath;
  aroma_ = aroma;
}

void DisplayManager::setNetworkInfo(bool wifiConnected, bool apMode, bool connectingSta,
                                     const String& ssid, const String& ip, const String& apSsid) {
  wifiConnected_ = wifiConnected;
  apMode_ = apMode;
  connectingSta_ = connectingSta;
  connectedSsid_ = ssid;
  ip_ = ip;
  apSsid_ = apSsid;
}

void DisplayManager::setBleConnected(bool connected) { bleConnected_ = connected; }

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

void DisplayManager::centerText(const char* text, int y) {
  int w = u8g2_.getStrWidth(text);
  int x = (128 - w) / 2;
  if (x < 0) x = 0;
  u8g2_.drawStr(x, y, text);
}

void DisplayManager::drawProgressBar(int x, int y, int w, int h, float fraction) {
  if (fraction < 0) fraction = 0;
  if (fraction > 1) fraction = 1;
  u8g2_.drawFrame(x, y, w, h);
  int fillW = (int)((w - 2) * fraction);
  if (fillW > 0) u8g2_.drawBox(x + 1, y + 1, fillW, h - 2);
}

void DisplayManager::drawRelayRow(const char* name, const RelayLogicalState& st, int y) {
  u8g2_.drawStr(0, y, name);
  int cx = 62, cy = y - 3, r = 3;
  if (st.logicalOn) {
    u8g2_.drawDisc(cx, cy, r);
  } else {
    u8g2_.drawCircle(cx, cy, r);
  }
  u8g2_.drawStr(72, y, st.logicalOn ? st.logicalOnLabel : st.logicalOffLabel);
}

void DisplayManager::drawQr(const String& text, int x, int y, int maxSize) {
  QRCode qrcode;
  uint8_t data[qrcode_getBufferSize(kQrVersion)];
  qrcode_initText(&qrcode, data, kQrVersion, ECC_LOW, text.c_str());

  int scale = maxSize / qrcode.size;
  if (scale < 1) scale = 1;

  for (uint8_t qy = 0; qy < qrcode.size; ++qy) {
    for (uint8_t qx = 0; qx < qrcode.size; ++qx) {
      if (qrcode_getModule(&qrcode, qx, qy)) {
        u8g2_.drawBox(x + qx * scale, y + qy * scale, scale, scale);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------

void DisplayManager::renderEstop() {
  u8g2_.setFont(u8g2_font_7x14B_tf);
  centerText(tr(Str::EstopLine1, language_), 24);
  centerText(tr(Str::EstopLine2, language_), 44);
  u8g2_.setFont(u8g2_font_6x10_tf);
  centerText(tr(Str::EstopHint, language_), 60);
}

void DisplayManager::renderTempText() {
  u8g2_.setFont(u8g2_font_6x10_tf);
  centerText(tempLine1_.c_str(), 28);
  centerText(tempLine2_.c_str(), 44);
}

void DisplayManager::renderPeekNetwork() {
  u8g2_.setFont(u8g2_font_6x10_tf);
  u8g2_.drawStr(0, 12, tr(Str::PeekNetworkTitle, language_));
  String line2, line3;
  if (apMode_) {
    line2 = tr(Str::PeekApMode, language_);
    line3 = apSsid_;
  } else if (wifiConnected_) {
    line2 = String(tr(Str::PeekWifiPrefix, language_)) + connectedSsid_;
    line3 = String(tr(Str::PeekIpPrefix, language_)) + ip_;
  } else {
    line2 = tr(Str::PeekWifiConnecting, language_);
  }
  u8g2_.drawStr(0, 28, line2.c_str());
  if (line3.length()) u8g2_.drawStr(0, 40, line3.c_str());
  u8g2_.drawStr(0, 56, tr(bleConnected_ ? Str::BleConnected : Str::BleFree, language_));
}

void DisplayManager::renderPeekApiKey() {
  u8g2_.setFont(u8g2_font_6x10_tf);
  u8g2_.drawStr(0, 12, tr(Str::PeekApiKeyTitle, language_));
  String key = apiKey_;
  while (key.length() < 32) key += ' ';
  u8g2_.drawStr(0, 32, key.substring(0, 16).c_str());
  u8g2_.drawStr(0, 46, key.substring(16, 32).c_str());
}

void DisplayManager::renderBoot() {
  u8g2_.setFont(u8g2_font_7x14B_tf);
  centerText(tr(Str::BootTitle, language_), 28);
  u8g2_.setFont(u8g2_font_6x10_tf);
  centerText(tr(connectingSta_ ? Str::BootConnectingWifi : Str::BootStarting, language_), 46);
}

void DisplayManager::renderConnectQr() {
  String wifiQr = String("WIFI:T:nopass;S:") + apSsid_ + ";;";
  drawQr(wifiQr, 2, 2, 60);

  u8g2_.setFont(u8g2_font_6x10_tf);
  u8g2_.drawStr(66, 14, tr(Str::QrScanTo, language_));
  u8g2_.drawStr(66, 28, tr(Str::QrConfigureWifi, language_));
  u8g2_.setFont(u8g2_font_5x8_tf);
  u8g2_.drawStr(66, 44, apSsid_.c_str());
  u8g2_.drawStr(66, 58, tr(bleConnected_ ? Str::BleConnected : Str::BleFree, language_));
}

void DisplayManager::renderStatus() {
  u8g2_.setFont(u8g2_font_7x14B_tf);
  const char* stateStr = "?";
  switch (safetyState_) {
    case SystemState::Disarmed: stateStr = "DISARMED"; break;
    case SystemState::Armed: stateStr = "ARMED"; break;
    case SystemState::Estop: stateStr = "ESTOP"; break;
  }
  centerText(stateStr, 11);

  // WiFi/BLE indicator at the top right (the bottom area is reserved for
  // the full-width safety cap bars — see below).
  u8g2_.setFont(u8g2_font_5x8_tf);
  String indicator;
  if (wifiConnected_) indicator += "W";
  if (bleConnected_) indicator += "B";
  if (indicator.length()) {
    int w = u8g2_.getStrWidth(indicator.c_str());
    u8g2_.drawStr(128 - w, 8, indicator.c_str());
  }

  u8g2_.drawHLine(0, 14, 128);

  u8g2_.setFont(u8g2_font_6x10_tf);
  drawRelayRow("Air", air_, 25);
  drawRelayRow("Re-Breath", rebreath_, 36);
  drawRelayRow("Aroma", aroma_, 47);

  // Full-width safety cap bars at the bottom of the screen — Air first
  // (on top) then Re-Breath, only when active. Fixed order so they stay
  // identifiable even without a label (which would eat up the width).
  if (airRemainingMs_ > 0) {
    float frac = (float)airRemainingMs_ / (float)airCapMs_;
    drawProgressBar(0, 50, 128, 6, frac);
  }
  if (rebreathRemainingMs_ > 0) {
    float frac = (float)rebreathRemainingMs_ / (float)rebreathCapMs_;
    drawProgressBar(0, 58, 128, 6, frac);
  }
}

void DisplayManager::render() {
  u8g2_.clearBuffer();

  if (safetyState_ == SystemState::Estop) {
    renderEstop();
  } else if (tempActive_) {
    renderTempText();
  } else if (peekPage_ == PeekPage::Network) {
    renderPeekNetwork();
  } else if (peekPage_ == PeekPage::ApiKey) {
    renderPeekApiKey();
  } else if (wifiConnected_ || bleConnected_) {
    renderStatus();
  } else if (apMode_) {
    renderConnectQr();
  } else {
    renderBoot();
  }

  u8g2_.sendBuffer();
}

void DisplayManager::tick() {
  if (!ready_) return;

  uint32_t now = millis();
  if (tempActive_ && (int32_t)(now - tempUntilMs_) >= 0) {
    tempActive_ = false;
  }
  if (peekPage_ != PeekPage::None && (int32_t)(now - peekUntilMs_) >= 0) {
    peekPage_ = PeekPage::None;
  }

  if (now - lastRenderMs_ < kRenderThrottleMs) return;
  lastRenderMs_ = now;
  render();
}
