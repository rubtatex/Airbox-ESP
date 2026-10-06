#pragma once

// =============================================================================
// DisplayManager — 1.3" I2C OLED screen (SH1106 128x64, U8g2 lib).
//
// Purely a display: makes NO safety decision. It only reflects the state
// provided by the other modules via the setters below, and AUTOMATICALLY
// picks which screen to show according to a fixed priority
// (see render()):
//
//   1. ESTOP                    — always takes priority, never hidden
//   2. Temporary text            — "display.text" action via API/BLE
//   3. Peek                      — network/API key, triggered by a short
//                                  press on the BOOT button, returns on its
//                                  own after a few seconds
//   4. Status (connected)        — as soon as WiFi OR BLE is connected:
//                                  ARMED/DISARMED at the top, the 3 relays
//   5. Connection QR code        — access point mode, no BLE client
//   6. Boot/connecting           — WiFi connection attempt in progress
//
// Sequences/automations live on the app side (phone), not on the
// ESP32 — see CLAUDE.md. Hence there is no "script running" screen.
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "SafetyManager.h"
#include "UiStrings.h"

enum class PeekPage : uint8_t { None, Network, ApiKey };

class DisplayManager {
 public:
  void begin();

  // Call regularly from loop() (throttled internally).
  void tick();

  // Short press on the BOOT button -> temporarily shows network/API key
  // (alternating between the two), returns to the normal screen on its own
  // after a few seconds. No effect during an ESTOP.
  void onShortPress();

  // Shows a temporary text ("display.text" action via API/BLE), returns
  // to the normal screen after durationMs. Takes priority over all but ESTOP.
  void showTemporaryText(const String& line1, const String& line2, uint32_t durationMs);

  // --- Data to reflect (updated by main.cpp) -------------------------------
  void setSafetyState(SystemState state, uint32_t rebreathRemainingMs, uint32_t rebreathCapMs,
                       uint32_t airRemainingMs, uint32_t airCapMs);
  void setRelayStates(const RelayLogicalState& air, const RelayLogicalState& rebreath,
                       const RelayLogicalState& aroma);
  void setNetworkInfo(bool wifiConnected, bool apMode, bool connectingSta, const String& ssid,
                       const String& ip, const String& apSsid);
  void setBleConnected(bool connected);
  void setApiKey(const String& apiKey) { apiKey_ = apiKey; }

  // Language of the displayed texts (English by default). See UiStrings.h
  // to add a language.
  void setLanguage(Language lang) { language_ = lang; }
  Language getLanguage() const { return language_; }

 private:
  U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2_{U8G2_R0, U8X8_PIN_NONE};
  bool ready_ = false;

  SystemState safetyState_ = SystemState::Disarmed;
  uint32_t rebreathRemainingMs_ = 0;
  uint32_t rebreathCapMs_ = 1;
  uint32_t airRemainingMs_ = 0;
  uint32_t airCapMs_ = 1;

  RelayLogicalState air_{};
  RelayLogicalState rebreath_{};
  RelayLogicalState aroma_{};

  bool wifiConnected_ = false;
  bool apMode_ = false;
  bool connectingSta_ = false;
  bool bleConnected_ = false;
  String connectedSsid_;
  String ip_;
  String apSsid_;
  String apiKey_;
  Language language_ = LANGUAGE_DEFAULT;

  PeekPage peekPage_ = PeekPage::None;
  uint32_t peekUntilMs_ = 0;
  static constexpr uint32_t kPeekDurationMs = 6000;

  bool tempActive_ = false;
  String tempLine1_;
  String tempLine2_;
  uint32_t tempUntilMs_ = 0;

  uint32_t lastRenderMs_ = 0;
  static constexpr uint32_t kRenderThrottleMs = 200;

  void render();
  void renderBoot();
  void renderConnectQr();
  void renderStatus();
  void renderEstop();
  void renderPeekNetwork();
  void renderPeekApiKey();
  void renderTempText();

  void drawQr(const String& text, int x, int y, int maxSize);
  void drawProgressBar(int x, int y, int w, int h, float fraction);
  void drawRelayRow(const char* name, const RelayLogicalState& st, int y);
  void centerText(const char* text, int y);
};
