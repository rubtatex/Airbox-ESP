# Airbox-ESP — API Reference (REST + WebSocket + BLE)

**WARNING — Read before writing the app**: this device controls the valves/pump of a breath-control system. The app must **always** display the safety state (`armed`/`disarmed`/`estop`) prominently, and must never let the user believe a relay command succeeded if the API answered `ok: false`.

This document is **self-contained**: everything needed to build the app (REST, WebSocket, BLE, safety model) is here; nothing requires access to the firmware source code.

## Overview

The Airbox-ESP unit (XIAO ESP32-C3) drives 3 relays controlling valves/pump:

| Relay (API name) | Role | Physical type | Meaning of "OPEN"/"logicalOn: true" |
|---|---|---|---|
| `air` | Air valve | Normally Open (NO) | Normal air available (default/safe state) |
| `rebreath` | Re-Breath valve | Normally Closed (NC) | Rebreathing active (requires `armed`, time-limited — see cap) |
| `aroma` | Aroma pump | — | Pump running |

The API always exposes the **logical** state of each relay (`OPEN`/`CLOSED`/`RUNNING`), never an ambiguous physical boolean — the app never needs to know the underlying NO/NC logic, it only displays/uses `logicalOn` and `label` as-is.

The device has a local OLED screen (status, connection QR code, etc.) — the app is an **additional control channel**, not the only way to interact with the unit.

### Automations/sequences: the app's responsibility

The firmware does **not** run any multi-step sequence on its own — no "scripts" stored or executed on the ESP32. If the app offers automations (e.g. "open Re-Breath for 5s, close, repeat 3 times"), the app orchestrates them by sending commands one by one (`relay.set`, etc.) with its own timing, using the atomic endpoints below.

**Important implication**: if the app loses its connection (BLE/WiFi) while a sequence is running, the sequence stops immediately — no further commands reach the firmware. This is not a firmware bug to fix; it is the expected behavior of this architecture. The app must:
- Detect a connection loss during a sequence and clearly warn the user (never let them believe the sequence is still running).
- Never assume a relay stayed in the intended state after reconnecting — always re-read the actual state (`GET /api/relays` or `STATE`) before resuming/displaying anything.
- Not rely on this architecture for safety-critical timing: the Re-Breath cap (see "Safety model") is enforced **by the firmware** independently of the app, precisely because a phone-driven sequence cannot be guaranteed to complete.

## Safety model (the app must respect it)

Three states, managed by the firmware:

```
DISARMED (idle state, including right after boot)
   → ARMED   (explicit action: physical button, app via arm())
   → ESTOP   (can be triggered at any time: button, app, or automatically)
        → ARMED via explicit re-arm only
```

- **`disarmed`**: no relay can be energized (so `air` stays open/safe, `rebreath` stays closed, `aroma` stays stopped). This is the idle state.
- **`armed`**: relay commands work normally.
- **`estop`** (emergency stop): all 3 relays are immediately forced to the de-energized (safe) state. An **explicit re-arm** (`POST /api/system/rearm`) is required to go back to `armed` — never automatic.
- De-energizing a relay (`state: false`) is **always allowed**, whatever the state — only energizing (`state: true`) requires `armed`.
- **Safety limits on `rebreath` AND `air`**: the firmware automatically cuts each of these two relays after a configurable duration — `rebreathCapMs` while `rebreath` is open, `airCapMs` while `air` is closed — (both **120s by default**, clamped between 5s and 300s) **whatever the source** of the command (REST, BLE, app). The two caps are **independent**: configuring one does not affect the other. The app should display these countdowns (`rebreathRemainingMs`, `airRemainingMs`) and warn the user before/at the automatic cut-off rather than let them believe the relay stays active indefinitely.
- **Emergency stop (`POST /api/system/estop` and the BLE `ESTOP` characteristic) never requires an API key**, on purpose: the safety brake must never be blockable by an authentication problem. The app must therefore always be able to call it, even when disconnected/out of sync on the key.

**UI recommendation**: an "EMERGENCY STOP" button always visible and functional (never greyed out because of auth), safety state displayed at all times, relay controls visually disabled until `armed` is reached.

## Base URL & authentication

- REST base URL: `http://airbox.local/api` (or the IP shown on the screen/serial port).
- Header required on every route **except** those listed in "Unauthenticated routes" below: `X-Api-Key: <key>`.
- The API key is randomly generated on first boot and persisted in NVS (it does not change across reboots, except via `POST /api/system/regenerate-key`). **Three ways to retrieve it from the app**, in order of convenience:
  1. **BLE**: connect over BLE (no WiFi needed) and read/subscribe to the `STATE` characteristic — the `apiKey` field is included in clear text, without authentication (see BLE section below). This is the recommended method for automatic first-time pairing.
  2. **REST during WiFi setup**: if the device has no WiFi configured yet, it broadcasts an `Airbox-XXXXXX` access point (also shown on the screen). Once connected to that access point, `GET http://192.168.4.1/api/system/info` returns an `apiKey` field — **only during this setup window**, no longer once connected to the regular WiFi.
  3. **OLED screen**: short press on the device's BOOT button (twice: network, then API key, 32 hex characters) — temporary screen that returns on its own after a few seconds. Useful as a fallback/for debugging without the app. No effect during an ESTOP.
- Standard error response: `{"error": "message"}` with an appropriate HTTP code (401 = invalid/missing key, 400 = invalid request, 404 = not found, 500 = server error).

### Unauthenticated routes (deliberate, see "Safety model" above)

- `GET /api/system/info` — discovery, before even having the key.
- `POST /api/system/estop` — the safety brake must never depend on a valid key.
- BLE `ESTOP` characteristic — same, over BLE.

Every other route requires authentication.

---

## System

### `GET /api/system/info` *(no auth)*
```json
{ "firmware": "airbox-esp", "uptimeMs": 123456, "freeHeap": 180000, "chipModel": "ESP32-C3" }
```
The `apiKey` field is added **only** while the device is in fallback access-point mode (initial WiFi setup, see Authentication section):
```json
{ "firmware": "airbox-esp", "uptimeMs": 4210, "freeHeap": 182000, "chipModel": "ESP32-C3", "apiKey": "c7e90609ca27f7bfb3851aa8a60c6f02" }
```

### `GET /api/system/status`
```json
{
  "safetyState": "armed",
  "rebreathRemainingMs": 12000,
  "rebreathCapMs": 120000,
  "airRemainingMs": 0,
  "airCapMs": 120000,
  "wifiConnected": true,
  "apMode": false,
  "ip": "192.168.1.42"
}
```

### `POST /api/system/reboot` → `{"ok": true}` then reboots (always comes back `disarmed`).

### `POST /api/system/factory-reset` → erases WiFi/API key/safety config, then reboots.

### `POST /api/system/regenerate-key` → `{"apiKey": "new_key"}`. The old key stops working immediately — only do this with the app at hand to re-pair.

### `POST /api/system/ota` — firmware update
**Raw** body (`Content-Type: application/octet-stream`), the firmware `.bin` image — **not** `multipart/form-data`. A plain binary upload is enough (`fetch(url, {method:"POST", headers:{"X-Api-Key":...}, body: fileArrayBuffer})` or `curl --data-binary @firmware.bin`).

Preconditions:
- Authentication required (`X-Api-Key`), like every control route.
- **The system must be `disarmed`**, otherwise `409`: `{"error": "refused: disarm the system before an OTA update"}`. Call `POST /api/system/disarm` (or `rearm` then `disarm`) before starting an update.

Responses:
- Success: `{"ok": true}` then the device immediately reboots into the new image (expect a few seconds of unavailability, as after `reboot`). Always comes back `disarmed`, like any power-up.
- Failure (invalid image, flash write failed, upload interrupted): `400` with an explicit error message. The **currently installed firmware is not affected** — nothing is applied, the device keeps running normally without rebooting.
- Invalid auth: `401`, like other routes.

No BLE equivalent: transferring more than 1 MB through a characteristic limited to 512 bytes makes no sense — use REST/WiFi for OTA.

During the upload, the device screen shows "Updating firmware / Do not power off" — inform the user in the app too, and avoid dropping WiFi/closing the app until the response is received.

### `GET /api/system/language`
```json
{ "language": "en", "available": ["en", "fr"] }
```
Returns the language currently displayed on the device's OLED screen (independent of the app's language — the app keeps its own language; this only drives the unit's physical screen).

### `PUT /api/system/language`
Body: `{"language": "fr"}` — a code from `available` (see `GET` above). An unknown/invalid code is rejected rather than silently falling back to a default.
Response: `{"ok": true, "language": "fr"}`, or `400` `{"error": "unknown language code"}` if the code is not in `available`.
Takes effect on the screen immediately (no reboot needed) and is persisted (NVS) — survives a reboot. **English by default out of the factory.**

---

## Safety *(the most important routes of the API — implement them first in the app)*

### `GET /api/system/safety-status`
```json
{
  "state": "disarmed",
  "rebreathRemainingMs": 0,
  "rebreathCapMs": 120000,
  "airRemainingMs": 0,
  "airCapMs": 120000
}
```
`state` ∈ `"disarmed" | "armed" | "estop"`.

### `POST /api/system/arm`
Goes `disarmed → armed`. Refused (`ok:false`) if the state is `estop` (`rearm` first).
```json
{ "ok": true, "state": "armed" }
```

### `POST /api/system/disarm`
Goes `armed → disarmed`, forces all 3 relays to the safe state. Refused if `estop`.

### `POST /api/system/estop` *(no auth)*
Immediate emergency stop: forces all 3 relays to the de-energized state. Always `{"ok": true, "state": "estop"}`.

### `POST /api/system/rearm`
Leaves `estop` and goes straight back to `armed`. Response `{"ok": false, "state": "disarmed"}` if called outside `estop`.

### `PUT /api/system/safety-config`
Body: `{"rebreathCapMs": 45000, "airCapMs": 90000}` — both fields are **independent and optional**: send only the one you want to change, the other stays unchanged in the firmware. Each value is automatically clamped to [5000, 300000] ms.
Response (reflects the current state of both caps, not only what was just sent): `{"ok": true, "rebreathCapMs": 45000, "airCapMs": 90000}`.

Examples:
- `{"rebreathCapMs": 45000}` → changes only the Re-Breath cap.
- `{"airCapMs": 90000}` → changes only the Air cap.
- `{"rebreathCapMs": 45000, "airCapMs": 90000}` → changes both in a single call.

---

## WiFi

### `GET /api/wifi/status`
```json
{ "connected": true, "apMode": false, "ip": "192.168.1.42", "ssid": "HomeWifi-XXXX" }
```
`ssid` is empty when not connected.

### `GET /api/wifi/scan`
**Asynchronous** scan: the first call starts the scan and returns `{"scanning": true}`; call the same route again until the list is returned (a new call after a list has already been delivered automatically starts a fresh scan):
```json
{
  "networks": [
    { "ssid": "HomeWifi-XXXX", "rssi": -52, "secure": true, "connected": true },
    { "ssid": "Neighbor-YYYY", "rssi": -78, "secure": true, "connected": false },
    { "ssid": "Cafe-Public", "rssi": -60, "secure": false, "connected": false }
  ]
}
```
`rssi` in dBm (closer to 0 = better signal), `secure` = false for an open network, `connected` = true for the network currently in use. Duplicates (same SSID seen from several access points) are de-duplicated, keeping the best signal; hidden networks (empty SSID) are ignored.

On the BLE side, the `STATE` characteristic exposes `wifiNetworks` in a lightweight form (`{ssid, rssi}`, without `secure`/`connected`, up to 4 networks) to stay under the hard 512-byte limit of a NimBLE characteristic — the full list (up to 10, with security and "connected" flag) is only available over REST.

### `POST /api/wifi/config`
Body: `{"ssid": "MyWifi", "password": "..."}`. Applies and attempts an immediate reconnection (may drop the current HTTP connection if the IP changes — plan a fallback to `airbox.local` or AP mode `192.168.4.1` in the app).

### `POST /api/wifi/forget`
Erases the WiFi credentials and returns to fallback access-point mode — **without** erasing the API key or safety config (unlike `factory-reset`). This is the endpoint to use to offer the user a "go back to local-only (BLE only)" option without losing their pairing. Response: `{"ok": true}`.

---

## Relays

**Logical** state (not the raw physical boolean) — `logicalOn: true` means "valve open" (air/rebreath) or "pump running" (aroma).

### `GET /api/relays`
```json
[
  { "name": "air", "energized": false, "logicalOn": true, "label": "OPEN" },
  { "name": "rebreath", "energized": false, "logicalOn": false, "label": "CLOSED" },
  { "name": "aroma", "energized": false, "logicalOn": false, "label": "STOPPED" }
]
```

### `GET /api/relays/{name}` — `{name}` ∈ `air | rebreath | aroma`. Same shape as one element of the array above.

### `POST /api/relays/{name}/set`
Body: `{"state": true}` (`true` = energize, `false` = de-energize — **always allowed**).
```json
{ "ok": true }
```
If `state:true` and the system is not `armed`:
```json
{ "ok": false, "error": "refused: system not armed (arm the system first)" }
```

### `POST /api/relays/{name}/toggle` — same semantics/response as `set`.

---

## Screen

### `POST /api/display/text`
Body: `{"line1": "Hello", "line2": "Short text", "durationMs": 3000}`. Displays temporarily (centered, 128x64 OLED), then returns to the normal status screen. Keep it reasonably short: no automatic line wrapping.

---

## Real-time WebSocket

`ws://airbox.local/ws?key=<api_key>` (the key is passed as a query parameter — browsers don't allow custom headers on the WebSocket upgrade).

- **Read-only**: the firmware pushes state; the app does not send commands over WS (use REST/BLE for that).
- On connection, the server immediately sends the current state.
- On every safety event or relay change, a message is broadcast to all connected clients:
```json
{
  "safetyState": "armed",
  "rebreathRemainingMs": 12000,
  "rebreathCapMs": 120000,
  "airRemainingMs": 0,
  "airCapMs": 120000,
  "wifiConnected": true,
  "apMode": false,
  "ip": "192.168.1.42",
  "event": "relay_changed"
}
```
`event` ∈ `"armed" | "disarmed" | "estop_triggered" | "rearmed" | "relay_changed" | "safety_cap_triggered"`. On `"safety_cap_triggered"`, check which of `rebreathRemainingMs`/`airRemainingMs` just dropped back to 0 to know which relay was cut automatically.

---

## BLE (NimBLE)

Custom **"Airbox Control"** service, advertised device name: `Airbox`.

| Role | UUID | Properties | Auth |
|---|---|---|---|
| Service | `9b1a0001-8f1a-4a2b-9c3e-1a2b3c4d5e6f` | — | — |
| `CMD` | `9b1a0002-8f1a-4a2b-9c3e-1a2b3c4d5e6f` | Write | API key in payload |
| `STATE` | `9b1a0003-8f1a-4a2b-9c3e-1a2b3c4d5e6f` | Read + Notify | — (read-only) |
| `ESTOP` | `9b1a0004-8f1a-4a2b-9c3e-1a2b3c4d5e6f` | Write | **none** (deliberate) |
| `WIFI_CONFIG` | `9b1a0005-8f1a-4a2b-9c3e-1a2b3c4d5e6f` | Write | API key in payload |

### Writing to `CMD`
JSON payload (UTF-8), `key` field mandatory:
```json
{ "key": "<api_key>", "cmd": "arm" }
{ "key": "<api_key>", "cmd": "disarm" }
{ "key": "<api_key>", "cmd": "rearm" }
{ "key": "<api_key>", "cmd": "relay.set", "relay": "aroma", "state": true }
{ "key": "<api_key>", "cmd": "relay.toggle", "relay": "aroma" }
{ "key": "<api_key>", "cmd": "safety.config", "rebreathCapMs": 45000, "airCapMs": 90000 }
{ "key": "<api_key>", "cmd": "display.text", "line1": "Hello", "line2": "Short text", "durationMs": 3000 }
{ "key": "<api_key>", "cmd": "wifi.scan" }
{ "key": "<api_key>", "cmd": "wifi.forget" }
{ "key": "<api_key>", "cmd": "system.language", "language": "fr" }
```
`wifi.scan` starts an asynchronous WiFi scan (works both in access-point mode and once connected); the result appears in `STATE` (`wifiScanning`, then `wifiNetworks`).

`wifi.forget` erases the WiFi credentials and returns to fallback access-point mode, without erasing the API key — BLE equivalent of `POST /api/wifi/forget` (useful to offer "go back to 100% local" without needing WiFi to do it).

`safety.config`: `rebreathCapMs`/`airCapMs` are independent and optional, exactly like `PUT /api/system/safety-config` — send only the field to change.

`system.language`: BLE equivalent of `PUT /api/system/language`. `language` ∈ `"en" | "fr"` (see `GET /api/system/language` over REST for the up-to-date list of available codes — same list on both sides). Takes effect on the screen immediately + persisted in NVS. Unknown code = command silently ignored (like any invalid BLE command, see below) — no explicit error response in BLE v1; check `STATE.language` to confirm the effect.

**Important**: once `wifiNetworks` is present, the JSON often exceeds the max size of a BLE notification (~250 bytes, MTU limit) — the **notification** may then arrive truncated (silently, without error). After sending `wifi.scan`, don't rely on the notification: do an **explicit read** of `STATE` (`readValue()`, not `notify`) in a loop until you see `wifiScanning: false` — a read doesn't have this size limit.
Any command with an invalid/missing key is silently ignored (no explicit error response in BLE v1 — watch `STATE` to confirm the effect).

No `script.*` commands: automations are handled by the app, see "Automations/sequences: the app's responsibility" at the top of this document.

### Writing to `ESTOP`
Any value written triggers the emergency stop immediately, without key verification.

### Writing to `WIFI_CONFIG`
```json
{ "key": "<api_key>", "ssid": "MyWifi", "password": "..." }
```

### Reading/subscribing to `STATE`
Notified automatically on every change (armed/disarmed/estop/relay/cap triggered), throttled to at most 1x/second. **Except for `wifiNetworks`**: see the warning above (`wifi.scan` section) — use an explicit read (`readValue()`), not the notification, for this specific data.
```json
{
  "state": "armed",
  "rebreathRemainingMs": 0,
  "airRemainingMs": 0,
  "apiKey": "c7e90609ca27f7bfb3851aa8a60c6f02",
  "language": "en",
  "air": { "open": true },
  "rebreath": { "open": false },
  "aroma": { "running": false },
  "wifiConnected": true,
  "wifiScanning": false,
  "wifiNetworks": [{ "ssid": "HomeWifi-XXXX", "rssi": -52 }, { "ssid": "Neighbor-YYYY", "rssi": -78 }]
}
```
The `apiKey` field is always present here (see Authentication section for why no key is required on this characteristic). `language` reflects the language currently displayed on the screen (`"en"`/`"fr"`, see `GET/PUT /api/system/language`). `wifiNetworks` only appears once a scan has completed (triggered via `cmd: "wifi.scan"`), limited to `{ssid, rssi}` for the 4 strongest networks — a BLE characteristic has a hard 512-byte limit on its value; the full list (up to 10, with security and "connected" flag) is only available over REST (`GET /api/wifi/scan`).

---

## Device discovery (important for iOS/Android)

The device advertises itself over mDNS as `airbox.local`, but mDNS support differs by platform:
- **iOS**: Bonjour/mDNS supported natively (`NWBrowser`/`NetServiceBrowser`), `airbox.local` usually resolves without any special setup.
- **Android**: no reliable `.local` resolution by default. Either use the `NsdManager` API (Network Service Discovery) to discover the `_http._tcp` service advertised by the device, or a third-party mDNS library (e.g. jmDNS), or **don't depend on `airbox.local` at all** on Android.

**Recommendation**: don't rely solely on the mDNS name. Robust cross-platform strategy:
1. On first pairing, retrieve the actual IP via `GET /api/wifi/status` (`ip` field) once on the same network, and store it in the app.
2. If the stored IP stops responding (network/DHCP change), fall back to BLE to rediscover the device and read its current IP — BLE doesn't depend on any network resolution.
3. `airbox.local` remains usable as a convenience/fallback (works directly on iOS, and on PC for testing), but don't make it the only path on Android.

## Typical flow for a new app

1. **First launch (factory state)**: automatic discovery of the device over BLE (no WiFi needed) → read `STATE`: this confirms the device responds AND directly provides the `apiKey` field to store in the app (no manual entry needed).
2. **Let the user choose**: stay 100% local (BLE only — handy away from home, requires no network infrastructure), or configure WiFi for REST/WebSocket control. Both cover all features (safety, relays) except the enriched WiFi discovery/scan, which is more complete over REST. This choice isn't final: `POST /api/wifi/forget` (or BLE cmd `wifi.forget`) lets the user go back to 100% local later without losing the pairing (API key).
3. If the user chooses to configure WiFi: write `WIFI_CONFIG` over BLE with the key retrieved in step 1, or connect to the `Airbox-XXXXXX` SoftAP (shown on the screen, with a QR code to join it directly) and use `POST /api/wifi/config` (the key can also be retrieved via `GET /api/system/info` while on this setup access point).
4. Once WiFi is up, switch to REST/WebSocket (more reliable/faster than BLE for continuous control) — retrieve and store the actual IP via `GET /api/wifi/status` (see "Device discovery" above, especially on Android).
5. Display the safety state at all times, disable relay controls until `armed` is reached, and always expose a clearly visible "EMERGENCY STOP" button that calls `POST /api/system/estop` (no need to worry about the API key).
6. For continuous control (live relays, app-driven sequences), prefer the WebSocket (`/ws`) to receive updates without polling the API in a loop — REST/BLE remain used to send commands.
