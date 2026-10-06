# Airbox-ESP — project memory

## WARNING — Nature of the project — read before any change

This is **not** a home-automation project. The firmware drives 3 relays of a 4-relay board that control the valves/pump of a **breath-control device**:

| Relay | Name | Type | Coil de-energized (idle/boot/crash/watchdog) | Coil energized |
|---|---|---|---|---|
| 1 | **Air** | Normally Open (NO) | **Valve OPEN → normal air (SAFE state)** | Valve closed |
| 2 | **Re-Breath** | Normally Closed (NC) | **Valve CLOSED → no rebreathing (SAFE state)** | Valve open → rebreathing active |
| 3 | **Aroma** | Pump | Stopped (default) | Running |

**Absolute guiding principle: the de-energized state is always the safe state.** The firmware must never try to "compensate" for a power loss by keeping a relay energized — the NO/NC mechanics guarantee safety; the code's only job is to never contradict them.

**Absolute architecture rule**: `RelayController` (low-level GPIO access) is **never** called anywhere other than from `SafetyManager`. All the rest of the firmware — `WebApi`, `BleManager`, `DisplayManager` — goes exclusively through `SafetyManager::setRelay()` / `toggleRelay()` to change a relay's state. Never bypass this choke point, not even "just for a test".

**Absolute documentation rule**: `API.md` is **self-contained** and will be distributed as-is (without this file) to the iOS/Android mobile app developer. Any change touching a REST route, a WebSocket message, or a BLE characteristic/command **must be reflected in `API.md` in the same session** — never let that doc drift from the actual firmware.

## Automation architecture: app-side, not embedded

**Decision made on 17/08/2026, after explicitly connecting it to the original request.** The very first need the user expressed was: *"automations loaded directly onto the board to avoid interruptions in case of a brief drop-out with the smartphone"*. An embedded script engine (`AutomationEngine`) was built for this, tested and working — then removed at the user's explicit request, with full knowledge of the trade-off:

- **Why removed**: simpler, lighter firmware (no instruction VM, no LittleFS storage, no DSL to maintain). The user considers brief drop-outs rare enough in real use to accept the trade-off.
- **What we lose**: if the phone loses its connection (BLE/WiFi) while a sequence is running on the app side, the sequence stops immediately (no more commands arrive) — not only on an actual power cut. The app must handle this (warn the user, never assume a relay remains in the intended state after a drop-out).
- **What does NOT change**: `SafetyManager` remains the sole gatekeeper. A relay still cannot be energized outside `ARMED`, and ESTOP/disarm still force the safe state immediately, whatever the source of the command (app, REST, BLE). The baseline safety guarantee is intact — only the guarantee of *completing a multi-step sequence* is dropped.
- **Practical consequence**: "automations" are now the responsibility of the application (phone), which chains already-existing atomic commands (`relay.set`/`toggle`, app-side `wait`, etc.) while driving the firmware live. No "script" route/command exists in the firmware anymore.

**If this choice is reconsidered later** (e.g. the user finds brief drop-outs are a real problem in practice), see the git history of this file and of `src/` before 17/08/2026 to recover `AutomationEngine` (flat-instruction VM, JSON DSL, `manual`/`on_arm`/`interval` triggers, LittleFS storage) — it was complete and working, not just sketched.

## Hardware & pinout (Seeed XIAO ESP32-C3)

| Function | XIAO pin | GPIO | Notes |
|---|---|---|---|
| 1.3" I2C OLED (SH1106) — SDA | D4 | GPIO6 | Imposed by the user |
| 1.3" I2C OLED (SH1106) — SCL | D5 | GPIO7 | same |
| 1.3" I2C OLED (SH1106) — VCC/GND | 3.3V / GND | — | Typical for this kind of module (unlike the previously used LCD1602, which required 5V) — **to be checked on the actual module once wired**; this firmware has not yet been tested with the OLED on the hardware. |
| Relay 1 — Air (NO) | D1 | GPIO3 | |
| Relay 2 — Re-Breath (NC) | D2 | GPIO4 | |
| Relay 3 — Aroma (pump) | D3 | GPIO5 | |
| BOOT button (already on the board) | D9 | GPIO9 | **Input only**, never driven as output (strapping pin). Long press (≥1s) = E-STOP/re-arm, short press = network/API key "peek" |

Pins deliberately **avoided** for relays: GPIO2, GPIO8, GPIO9 (ESP32-C3 strapping pins — safety firmware must never risk disturbing boot).

**History**: the firmware first used an I2C LCD1602 (kept as of 17/08/2026 after a 3.3V contrast issue, solved by moving its VCC to 5V). It was later replaced by a 1.3" I2C OLED (SH1106) at the user's request: the LCD1602 could not show the state of all 3 relays at once (only 2 lines of 16 characters) and could not display a QR code. See git/file history for the details of the old LCD1602 implementation (`hd44780` lib) if it ever needs to come back.

**Hardware assumptions to verify before final wiring**:
1. Relay polarity: the firmware assumes **active LOW** triggering (`Config.h::RELAY_ACTIVE_LOW`). To be checked on the actual board.
2. The relay board inputs must have a pull-up that keeps them de-activated while the XIAO GPIO is high-impedance (state at the very start of boot, before `setup()`, and during flashing). Otherwise a flash/reset could momentarily energize a relay.
3. OLED I2C controller/address: firmware configured for **SH1106** (confirmed by the user). Address usually `0x3C` — the boot-time I2C scan (`DisplayManager::begin()`) prints it on the serial port for verification. If the screen stays black, check the I2C scan first (device detected or not) and the VCC voltage — see the LCD1602 mishap above for the diagnostic method.

## Safety model — the heart of the firmware

### States (`SafetyManager`, `src/SafetyManager.h/.cpp`)

```
DISARMED (default on EVERY boot, including normal power-up)
   -> ARMED   (explicit action: button, API, BLE)
   -> ESTOP   (can be triggered from any state)
        -> ARMED via reArm() only (manual re-arm required)
```

- **The system always boots `DISARMED`**, even after a clean reboot. No relay command can energize anything until the user has explicitly armed the system. This prevents a crash/watchdog/power cut from automatically restarting a session.
- **`SafetyManager::setRelay(id, energized)`** is THE gatekeeper: de-energizing (going to the safe state) is **always** allowed whatever the system state; energizing (`true`) is only allowed when `ARMED`. It is the only place in the firmware that decides whether a relay command is allowed.
- **ESTOP**: immediately forces all 3 relays to the de-energized state. Can be triggered by:
  - long press (~1s) on the BOOT button,
  - `POST /api/system/estop` (**no authentication** — see below),
  - writing to the BLE `ESTOP` characteristic (**no authentication** either).
- **Re-arming**: `POST /api/system/rearm` (authenticated) or button gesture (long press while in ESTOP).

### Why ESTOP is not authenticated

Deliberate choice: the safety brake must **never** be blockable by a forgotten/lost/expired API key. The worst case of a wrongly triggered ESTOP is an interruption; it is never dangerous. Conversely, `arm`, `disarm`, `rearm` and any command that *energizes* a relay remain authenticated.

### Hardware watchdog

There is **no** physical cut-off of the relay power supply independent of the code (user's choice). The firmware therefore relies on the **ESP32 hardware watchdog** (`esp_task_wdt`, ~4s, configured in `SafetyManager::begin()`), fed on every `tick()` of the main loop. If `loop()` hangs for any reason, the chip resets → GPIOs return to their default state (safe, see assumption #2 above) → the firmware restarts `DISARMED`.

**Consequence for all code**: `loop()` must **never** contain `delay()` or any blocking wait. Everything is driven by `millis()` and non-blocking state machines (`SafetyManager::tick()`, `NetworkManager::tick()` with asynchronous WiFi scan).

### Hard limits — Re-Breath cap AND Air cap (independent)

`SafetyManager` starts an internal timer (`CapTimer`) as soon as **Re-Breath** OR **Air** becomes energized — for Air, "energized" means valve **closed** (NO, so no normal air); that is the state being capped, symmetrically to Re-Breath open. **Whatever the source** (REST, BLE, app — everything goes through `setRelay()`).

- Default **120000 ms (120 s)** for both (`Config.h::REBREATH_CAP_DEFAULT_MS` / `AIR_CAP_DEFAULT_MS`, raised from 60 s on 17/08/2026 at the user's request).
- Configurable **independently** via `PUT /api/system/safety-config` (or BLE cmd `safety.config`): each field (`rebreathCapMs`, `airCapMs`) is optional in the body — sending only one field leaves the other unchanged. Explicit user decision (one setting per valve, not a global setting).
- Each clamped to [5s, 300s] (`REBREATH_CAP_MIN_MS`/`MAX_MS`, `AIR_CAP_MIN_MS`/`MAX_MS`) so the safeguard can never be disabled by mistake.
- Persisted in NVS (`StorageManager::getRebreathCapMs/setRebreathCapMs`, `getAirCapMs/setAirCapMs`) to survive a reboot.
- When either expires, the relay concerned is automatically forced de-energized and a `SafetyEvent::Type::SafetyCapTriggered` event is broadcast (WS + BLE + screen), with the `relay` field indicating which of the two expired — **no automatic re-opening**, a new explicit command is required.

Implementation: a single `struct CapTimer { capMs, deadlineMs, active }` is instantiated twice in `SafetyManager` (`rebreathCap_`, `airCap_`). `setRelay()` starts/stops the appropriate timer on each energization transition; `checkCaps()` (called from `tick()`) checks both on every iteration; `forceAllSafeInternal()` (ESTOP/disarm) stops both.

## Software architecture

```
src/
  main.cpp                 non-blocking setup()/loop(), wires all modules together
  Config.h                 pins, BLE UUIDs, default safety constants
  RelayController.h/.cpp   low-level GPIO + logical state computation (NO/NC/pump). Called ONLY by SafetyManager.
  SafetyManager.h/.cpp     THE safety gatekeeper (see previous section)
  DisplayManager.h/.cpp    1.3" I2C OLED SH1106 (U8g2 lib) — AUTOMATIC screen selection by fixed priority (see "Display model" below), connection QR code (QRCode lib)
  StorageManager.h/.cpp    Preferences/NVS only (WiFi, API key, Re-Breath cap) — no file storage
  NetworkManager.h/.cpp    WiFi STA (NVS credentials) + SoftAP fallback on failure/absence, mDNS "airbox.local", async WiFi scan
  BleManager.h/.cpp        NimBLE, custom "Airbox Control" service (see API.md for UUIDs)
  WebApi.h/.cpp            ESPAsyncWebServer + WebSocket, all REST routes (see API.md)
```

## Display model (`DisplayManager`)

The screen **automatically** chooses what to display, by fixed priority order (the first one that applies wins):

1. **ESTOP** — always top priority, never hidden by anything else.
2. **Temporary text** — `display.text` action via REST/BLE (e.g. to signal the end of an app-driven sequence).
3. **"Peek"** — triggered by a short press on the BOOT button: cycles network then API key, returns to the normal screen on its own after ~6s (`kPeekDurationMs`). No effect in ESTOP.
4. **Status** — as soon as WiFi **or** BLE is connected: ARMED/DISARMED/ESTOP centered at the top, "W"/"B" connectivity indicator in the **top right** (moved from bottom right on 17/08/2026 to free the full width at the bottom for the cap bars), a separator line, then the 3 relays with their logical state (filled dot = open/running, empty = closed/stopped). If a cap is active, a **full-width progress bar** appears at the bottom: Air first (y=50) if its cap is active, then Re-Breath (y=58) — fixed order, each one shown only if its corresponding `remainingMs` is > 0. Before 17/08/2026 there was only a small bar next to the Re-Breath line; it was removed in favor of this new layout.
5. **Connection QR code** — access-point mode (`apMode_`) AND no BLE client connected: QR code in `WIFI:T:nopass;S:<ssid>;;` format (the SoftAP is open, no password) to join the fallback access point directly from the phone's camera, + SSID as text next to it.
6. **Boot/connecting** — default screen during an ongoing WiFi connection attempt (neither connected nor yet switched to AP, see `NetworkManager::isConnectingSta()`).

All the selection logic is in `DisplayManager::render()` — it's the only place to modify to change priorities. No "script running" screen: sequences live on the app side (see "Automation architecture" at the top of this file).

### QR code

Generated with `ricmoo/QRCode`. **QR version fixed at 6** (`kQrVersion` in `DisplayManager.cpp`), deliberately large relative to the actual content (~32 characters): this lib does not resize the buffer automatically if the text doesn't fit the requested version — a too-small buffer would be a memory overflow, not just a broken QR. A comfortable margin (the QR simply appears a bit smaller on screen) is better than a risk of memory corruption.

## Screen internationalization (`UiStrings.h/.cpp`)

**Decision of 17/08/2026**: the interface shown on the OLED must be **in English by default**, with the option to add other languages, and an endpoint (REST **and** BLE) to change the language. Explicitly **not** requested for `test-app/` (developer test page, stays in French; only the labels it sends to/receives from the firmware change language).

- **`enum class Language : uint8_t { English = 0, French = 1 }`**, `LANGUAGE_DEFAULT = Language::English`, `LANGUAGE_COUNT = 2`. Adding a language = adding an enum value + a column in the translation table, nothing else to touch on the logic side.
- **`enum class Str { ... }`**: one value per translatable string shown on screen (e.g. `EstopLine1`, `BootTitle`, `QrScanTo`, `PeekApiKeyTitle`...).
- **`tr(Str, Language) -> String`**: lookup in a static table `kTable[Str::Count][LANGUAGE_COUNT]` defined in `UiStrings.cpp`. English is the implicit fallback (first column).
- **`languageCode(Language) -> "en"/"fr"`** and **`languageFromCode(const String&, Language&) -> bool`**: conversion to/from the code used by the API (REST/BLE), with validation (unknown code = `false`, output left untouched).
- **Persistence**: `StorageManager::getLanguage()/setLanguage(Language)`, NVS key `"language"` stored as `uint8_t`, default `LANGUAGE_DEFAULT`.
- **Critical boot order (`main.cpp`)**: `storage.begin()` → `display.setLanguage(storage.getLanguage())` → **only then** `display.begin()`. The language must be applied **before** the very first boot splash is rendered, otherwise boot always shows the default English for one cycle before switching.
- **API exposure**: `GET/PUT /api/system/language` (REST) and `cmd:"system.language"` (BLE), both call **both** `display_->setLanguage()` (immediate on-screen effect) and `storage_->setLanguage()` (persistence). See `API.md` for request/response details.
- `DisplayManager::renderStatus()` does **not yet** use `tr()` for the state literals ("DISARMED"/"ARMED"/"ESTOP" and the relay labels "Air"/"Re-Breath"/"Aroma"/"OPEN"/"CLOSED"/"RUNNING"/"STOPPED") — these strings remain in English in the `RelayController`/`DisplayManager` source regardless of the chosen language. Since English is the project's default language, this breaks nothing as long as no other language needs to translate them; if French (or another language) ever needs to translate these labels too, they will have to go through `tr()` at that point.

### Resolved Windows pitfall: `Strings.h` / `strings.h` collision

The file was named `UiStrings.h`/`.cpp` and **never** `Strings.h`/`.cpp`. A first attempt with `Strings.h` caused an opaque compile error (`'String' does not name a type`) in files that didn't even include that header directly (`RelayController.cpp`, `StorageManager.cpp`...). Cause: Windows resolves include paths **case-insensitively**; `#include <strings.h>` (a real standard POSIX header, included very early and indirectly by the toolchain, **before** Arduino's `String` type is declared) matched the project's `src/Strings.h` instead of the expected system header. **Never recreate a file named `Strings.h`/`.cpp` in this project** — always `UiStrings.h`/`.cpp` or another name that doesn't collide with a standard C/POSIX header.

## OTA (over-the-network firmware update)

**Decision of 18/08/2026, reversing the initial "no OTA in v1" choice.** Until then the partition table (`no_ota.csv`) reserved only **one** application slot (`app0`, subtype `ota_0`, 2 MB): physically there was nowhere to write a new image while the old one was running — OTA was therefore impossible regardless of application code, not just "not implemented". Switched to **`min_spiffs.csv`**: two `ota_0`/`ota_1` slots of 1,966,080 bytes (~1.875 MB) each + minimal `spiffs` (128 KB, unused) + `coredump` (64 KB) — exactly 4 MB, the flash size of the XIAO ESP32-C3. The current firmware (~1.39 MB) fits with ~560 KB (~29%) of headroom per slot.

### Endpoint

`POST /api/system/ota` (REST only — no BLE equivalent; transferring more than 1 MB through a BLE characteristic limited to 512 bytes would make no sense). **Raw** body (`Content-Type: application/octet-stream`), the `.bin` image as produced by `pio run` — **no** `multipart/form-data`, to stay consistent with the rest of the API (already JSON, never forms) and so that a simple `fetch()`/`curl --data-binary` is enough on the app side. See `API.md` for response details.

- **Authentication required** (`X-Api-Key`) — unlike `ESTOP`, this is a destructive high-privilege operation, not a safety brake.
- **Requires the `DISARMED` state**, refused (409) otherwise: symmetric to the rule "energizing a relay requires `ARMED`" — replacing the firmware requires the opposite; no sequence may overlap a flash.
- Checked from the **first byte received** (before `Update.begin()`), not after fully buffering like the other `jsonBody()` handlers — an OTA upload is >1 MB, unlike a JSON body; buffering/writing to flash an upload we are going to reject anyway is not trivial.
- Written to flash on the fly via `Update.write()` (no accumulation in RAM), into the **inactive** OTA slot — the running slot is never touched.
- At the end: `Update.end(true)` validates the image (checksum/magic byte). Success → `{"ok":true}` then `ESP.restart()` (like `reboot`/`factory-reset`, same conventions). Failure (`begin`/`write`/`end`) → 400 with `Update.errorString()`, the current firmware keeps running normally, nothing is applied.
- During the upload, the screen shows "Updating firmware / Do not power off" (`display.text`, 60s) — "temporary text" priority, see "Display model".

### Why it fails safe even with a bad image

The currently running firmware (slot A) is never modified while the new image is written to the inactive slot (slot B) — an interrupted upload (WiFi dropped, network error) simply leaves slot B incomplete/invalid, without touching slot A which keeps working. `otadata` (the "which slot to boot" pointer) is only updated **after** a successful `Update.end(true)`.

If a valid-but-buggy image is nevertheless flashed successfully and boot-loops or crashes before reaching a working state: the consequence is **not dangerous**, consistent with the project's guiding principle ("the de-energized state is always the safe state", see the very top of this file) — an ESP32 that crashes/reboots in a loop leaves the GPIOs in their default state (high impedance), so Air open/Re-Breath closed/Aroma stopped, indefinitely. The worst case of a bad OTA is an unresponsive device (requires a USB reflash), never a device stuck in a dangerous active state.

`esp_ota_mark_app_valid_cancel_rollback()` is called early in `setup()` (`main.cpp`) — confirms the booting image is valid, to cancel any pending automatic rollback. **Note**: this firmware does not enable the ESP-IDF bootloader's automatic rollback (would require a custom `sdkconfig`, non-trivial with the Arduino framework under PlatformIO) — so this call is mostly defensive/no-op in the current state, but costs nothing and prepares the ground if rollback is enabled later. **Practical consequence**: no automatic rollback if a buggy image boots "successfully" but misbehaves without crashing outright — only a hard crash (boot loop) leaves the relays in their default safe state; a subtler bug that leaves the firmware running but misbehaving is NOT caught automatically. Testing a new image over USB before distributing it via OTA remains the recommended practice.

## Technical choices (and why)

- **U8g2** (replaces `hd44780`/LCD1602) for the 128x64 SH1106 OLED: full graphics lib (fonts, shapes, `drawBox`/`drawDisc`/`drawFrame`...) needed for screen selection, progress bars and pixel-by-pixel QR code rendering. Full framebuffer mode (`_F_`, not `_1_`): 1 KB of RAM for the buffer, negligible on this ESP32-C3.
- **QRCode (ricmoo)**: generates the WiFi connection QR code, then rendered via U8g2 (`drawBox` per module). See the "QR code" section above for the version choice.
- **NimBLE-Arduino** rather than Arduino-ESP32's historical BLE stack: much smaller RAM footprint, needed to run WiFi + async web server + BLE simultaneously on an ESP32-C3 (single core, limited RAM). RAM usage measured with the whole firmware assembled (OLED+QR included, without AutomationEngine/LittleFS): ~17% (55 KB / 320 KB).
- **ESPAsyncWebServer / AsyncTCP (`ESP32Async/*` forks)**: actively maintained forks, compatible with the Arduino-ESP32 3.x core used here.
- **No hardware RTC** (user decision): no firmware feature depends on absolute time.
- **Preferences/NVS only** for persistence (WiFi, API key, Re-Breath cap, Air cap, language) — no LittleFS/file storage: automations live on the app side, see "Automation architecture" at the top of this file.
- **`min_spiffs.csv` partition** (`platformio.ini`): two OTA application slots (`ota_0`/`ota_1`, ~1.875 MB each) + minimal `spiffs` (128 KB, unused — no LittleFS/SPIFFS). Flash used: ~71% (1.39 MB / 1.875 MB per slot). See the "OTA" section above for the decision and its history (`no_ota.csv` used until 18/08/2026).
- **API key randomly generated on first boot** (`StorageManager::getApiKey()`). Every REST/BLE control route requires it, except `POST /api/system/estop` / BLE `ESTOP` characteristic (see "Why ESTOP is not authenticated" above). **Three ways to retrieve it** (so as not to depend on a cable/serial monitor):
  1. **OLED screen**: API key "peek" page, reached with a short press on the BOOT button (2 presses: network then API key), returns to the normal screen on its own after a few seconds. Requires physical access to the device.
  2. **BLE, `STATE` characteristic**: included in clear text in the notified JSON, **without authentication**. Deliberate choice: reading `STATE` over BLE already requires physical proximity (range of a few meters), a security barrier comparable to reading the screen. Lets an app pair automatically without manual re-entry.
  3. **REST, `GET /api/system/info`**: the key is included **only while the device is in access-point mode** (`network_->isApMode()`), i.e. during the initial pairing window when the device broadcasts its own dedicated WiFi network. Once connected to the regular home WiFi, this route no longer returns the key (exposure surface reduced to the strict minimum).
  - The serial port (`Serial.println` at boot, 115200 baud) remains available too, as a fallback/for debugging.
- **No Home Assistant/MQTT integration** (not a home-automation use case). **xtoys.app compatibility dropped** — xtoys doesn't support Bluetooth for "custom toys"; revisit only if that changes.
- **Shared REST/BLE WiFi scan** (`NetworkManager`): the asynchronous scan (`WiFi.scanNetworks(true)`) is cached non-destructively (`lastScanResults_` + `scanGeneration_` incremented on each completed scan), so that REST and BLE can each read/request a scan independently without stealing each other's results. `WebApi` keeps its own `lastSeenWifiScanGen_` to deliver each scan exactly once to a REST client, then automatically starts the next one. The fallback access-point mode uses `WIFI_AP_STA` (not `WIFI_AP` alone) to allow scanning surrounding WiFi during setup.
- **CORS enabled on the REST API** (`WebApi::begin()`, `DefaultHeaders` + handling of preflight `OPTIONS` requests in `onNotFound`): solely to let `test-app/` (web page served from another origin) call the API via `fetch()`. Doesn't affect any security behavior — the `X-Api-Key` key is still required exactly as before; this only adds HTTP response headers.

## BLE UUIDs (custom "Airbox Control" service)

Defined in `Config.h`. Full protocol details in `API.md`.

- Service: `9b1a0001-8f1a-4a2b-9c3e-1a2b3c4d5e6f`
- `CMD` (write, JSON, API key required): `9b1a0002-...`
- `STATE` (read/notify, JSON, pushed automatically): `9b1a0003-...`
- `ESTOP` (write, **no API key**): `9b1a0004-...`
- `WIFI_CONFIG` (write, JSON, API key required): `9b1a0005-...`

## PlatformIO commands

```
pio run                 # build
pio run -t upload       # flash
pio device monitor      # serial monitor (115200 baud) — shows the API key at boot
```

On the original development machine, `pio` is not in the PATH: use the full path `%USERPROFILE%\.platformio\penv\Scripts\pio.exe` (or the PlatformIO VS Code extension).

## NimBLE pitfall not to repeat

`NimBLECharacteristic::setValue()` has **no** dedicated `const char*` overload — only `setValue(const uint8_t*, size_t)`, `setValue(const std::vector<uint8_t>&)`, and a generic template `setValue(const T&)`. If you pass it a raw `const char*` (e.g. `myString.c_str()`), overload resolution falls into the "POD type" template branch (`NimBLEAttValue`, via `Has_c_str_len<T>` which fails for a pointer): it copies **the pointer's address** (`sizeof(T)` = 4 bytes on ESP32) instead of the pointed-to text. Result: the characteristic contains 4 bytes of "garbage" (actually the memory address), not the expected text — silent, no compile or runtime error. This is exactly what happened on `STATE` (`BleManager.cpp`), discovered by adding a debug `onRead` callback that logged `len=4`.

**Always pass the `String` object directly to `setValue()`** (`chrState_->setValue(myString)`), never `.c_str()` — `String` has `c_str()`/`length()`, which selects the right template overload (`Has_c_str_len<T>::value == true`).

## BLE pitfall not to repeat: truncated notifications

A BLE **notification** (`characteristic->notify()`) is limited to `MTU-3` bytes (~250 with `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=255`) and **silently truncated** beyond that — no error, no exception, just cut data. An **explicit read** (`readValue()`) doesn't have this limit: it uses the "read blob" mechanism that chains several requests to deliver a complete value, whatever its size (up to the NimBLE max, 512 bytes by default).

This happened with `STATE`: the base JSON (~230 bytes) fits in a notification, but as soon as `wifiNetworks` is added, the total exceeds the limit and the notification arrived truncated in the browser (`JSON.parse` failed silently). The value STORED in the firmware is always correct and complete (`setValue()` is not affected, only the notification transmission is) — an explicit read therefore retrieves it in full.

A second limit to know, different from the notification one: a NimBLE characteristic has a **hard** maximum of 512 bytes (`BLE_ATT_ATTR_MAX_LEN`) for its stored value itself, notification or not. Beyond that, `NimBLEAttValue::setValue()` returns `false` and **keeps the old value** (no crash, but stale data served indefinitely until the size is reduced). That's why `wifiNetworks` over BLE (`BleManager::buildStateJson()`) is deliberately limited to 4 networks and `{ssid, rssi}` only (no `secure`/`connected`) — the full, enriched list remains available over REST (`GET /api/wifi/scan`), which has no such constraint.

**Rule**: for any data whose size can vary/grow (lists, scan results...), the client must retrieve it via an explicit read (`readValue()`) after triggering the action, never by relying on the notification that followed. See `test-app/ble.js::readState()` and its use in `app.js` for the BLE WiFi scan.

## Known limitations / future improvement ideas

- No physical cut-off of the relay power supply independent of the firmware — safety relies entirely on firmware + watchdog. **Adding a hardware emergency-stop button that cuts relay power independently of the ESP32 would be the best possible safety improvement** (discussed with the user, declined for now).
- **No embedded automations** (settled decision, see "Automation architecture" at the top of this file): an app-driven sequence stops if the phone loses the connection midway.
- No reliable absolute time (no RTC).
- The AP captive portal (fallback SoftAP) has no DNS redirection to force a config page to open automatically on the phone — `DNSServer` already redirects everything to the AP's IP, but there is no basic HTML provisioning page served by `WebApi` in AP mode yet (provisioning is currently done via the REST API or BLE).
- **The OLED screen (SH1106) has still not been visually confirmed on the real hardware** at the time of writing — the physical hardware has not yet been re-wired (LCD1602 → OLED, VCC 5V → 3.3V) since the component change. It compiles and flashes without errors, but must be checked at power-up: I2C address detected (serial log at boot), VCC voltage, readability of each screen (status, QR scanned with a phone, peek, ESTOP).
