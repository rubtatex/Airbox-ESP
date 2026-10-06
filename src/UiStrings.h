#pragma once

// =============================================================================
// UiStrings — internationalized OLED screen texts. English = default and
// reference language (fallback if a text is missing in another language).
//
// NOTE: deliberately named "UiStrings" and not "Strings" — on a
// case-insensitive filesystem (Windows), "Strings.h" collides with the
// standard POSIX header "strings.h" and breaks the build in a very
// confusing way (the compiler silently includes OUR file instead of the
// system header, well before Arduino.h has defined `String`).
// Never rename this file to "Strings.h".
//
// To add a language: add it to `Language`, increment LANGUAGE_COUNT,
// add a column to the table in UiStrings.cpp, and accept it in
// languageFromCode(). Nothing else to change anywhere in the
// firmware — DisplayManager only uses tr(Str::..., language).
// =============================================================================

#include <Arduino.h>

enum class Language : uint8_t { English = 0, French = 1 };
static constexpr uint8_t LANGUAGE_COUNT = 2;
static constexpr Language LANGUAGE_DEFAULT = Language::English;

enum class Str : uint8_t {
  EstopLine1,
  EstopLine2,
  EstopHint,
  BootTitle,
  BootStarting,
  BootConnectingWifi,
  QrScanTo,
  QrConfigureWifi,
  BleConnected,
  BleFree,
  PeekNetworkTitle,
  PeekApMode,
  PeekWifiPrefix,
  PeekIpPrefix,
  PeekWifiConnecting,
  PeekApiKeyTitle,
  Count
};

// Text for `id` in language `lang` (falls back to English if missing).
const char* tr(Str id, Language lang);

// Short code ("en", "fr"...) used in the REST/BLE API.
const char* languageCode(Language lang);

// Resolves a code ("en"/"fr") to a Language. false if the code is unknown.
bool languageFromCode(const String& code, Language& out);
