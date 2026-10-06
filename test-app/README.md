# Airbox-ESP — Test app (REST + WebSocket + BLE)

A static web page, with no dependencies and no build step, for testing **every** feature of the Airbox-ESP firmware (safety, relays, WiFi, screen) over WiFi and/or Bluetooth. Full route reference: [`../API.md`](../API.md).

Note: the firmware does not run sequences/automations itself (see `../API.md`, section "Automations/sequences"). This page therefore only offers atomic commands, the way a real app that orchestrates its own sequences would.

**WARNING**: this is a raw developer test tool, not the final app. This device controls the valves/pump of a breath-control system: always keep an eye on the safety state badge (ARMED/DISARMED/ESTOP) before sending a relay command.

## Running the page

The files **must** be served from `http://localhost` (Web Bluetooth doesn't work on a page opened directly as `file://`). From this folder:

```
python -m http.server 8080
```
then open **http://localhost:8080** in the browser.

(No Python? Any local static server works: `npx serve .`, the "Live Server" VS Code extension, etc.)

## Browser requirements

- **WiFi/REST/WebSocket**: any modern browser.
- **Bluetooth (BLE)**: **Chrome, Edge or Opera only** (Web Bluetooth). Firefox and Safari/iOS don't support it.
  - On a PC you need a Bluetooth adapter. Otherwise, open the same page from an Android phone with Chrome. For BLE alone, the phone doesn't need to be on the same WiFi network as the Airbox.

## Two separate connections

The page deliberately keeps two independent connections, each in its own panel, with no side effects on each other:

- **WiFi connection (REST + WebSocket)**: base URL + API key, used for all `fetch()`/WebSocket calls. Periodic refresh of the safety badge only starts after clicking **"Save and connect"**, never automatically when the page loads.
- **Bluetooth connection (BLE)**: fully independent, with its own Connect/Disconnect buttons.

The BLE module reads the API key directly from the `STATE` characteristic (readable without authentication) and uses it for its own commands, so it never needs the WiFi panel's key field. The only bridge between the two is an explicit button in the BLE panel, **"Copy the key received over BLE into the WiFi field"**. It's only useful if you also want to use that key over REST; nothing is copied automatically.

## First launch: getting the API key

Simplest way: in the Bluetooth panel, click **"Connect over BLE"**. This reads the device state, including the key; that read needs no authentication (see `../.claude/CLAUDE.md`). Then click **"Copy the key received over BLE into the WiFi field"** to use it over REST too.

Without BLE: if the Airbox's WiFi isn't configured yet, it broadcasts an `Airbox-XXXXXX` access point (also shown on the OLED screen, with a QR code to join it directly). Connect your PC/phone to that network, set `http://192.168.4.1` as the base URL, and click **"GET /system/info"**. The key is included as long as the device is in access-point mode.

To go back to fully local mode without losing the key/pairing, use **"Forget WiFi"** in the System panel (REST), or `wifi.forget` in the Bluetooth panel (BLE).

## Scanning WiFi

Two independent ways, each in its own panel:
- **WiFi (REST)**: the **"Scan"** button in the WiFi panel. Each click starts a new scan and shows the result as soon as it's ready.
- **Bluetooth (BLE)**: the **"Scan WiFi over BLE"** button in the BLE panel. Useful for finding the SSID to configure without any active WiFi connection, including while the device is still in access-point mode.

## CORS

The firmware must send CORS headers so this page (served on `localhost`) can call the Airbox REST API (a different origin). `WebApi.cpp` already does this. With an older firmware that lacks it, REST calls fail with a CORS error in the browser console. WebSocket and BLE are not affected.

## Page layout

- **Connection**: base URL + API key (persisted in the browser via `localStorage`).
- **Safety**: live state badge, ARM/DISARM/REARM, big ESTOP button (works even without a key entered), Re-Breath cap configuration.
- **System**: info/status, reboot, factory-reset, key regeneration, forget WiFi.
- **Firmware update (OTA)**: pick a `.bin` file (the one produced by `pio run`, in `.pio/build/seeed_xiao_esp32c3/firmware.bin`) and upload it with a progress bar. Requires the system to be `disarmed` (the firmware refuses otherwise with a 409). The device reboots automatically after a successful flash.
- **WiFi**: status, graphical scan (signal bars, secured/open), configuration.
- **Relays**: one card per relay (air/rebreath/aroma) with its logical state (OPEN/CLOSED/RUNNING) and set/toggle. Refreshes automatically every 4s while the WiFi connection is active (`/api/relays`; relay states are not part of the broadcast status/WS, see `API.md`), and on every state received over BLE.
- **Screen**: send temporary text to the OLED.
- **WebSocket**: real-time event stream.
- **Bluetooth**: mirrors every REST command over BLE (arm/disarm/rearm/relay.set/relay.toggle/safety.config/display.text/wifi.scan/wifi.forget), plus ESTOP/WIFI_CONFIG/STATE.
- **Activity log**: all REST requests/responses, BLE writes/notifications and WS messages, timestamped.

## Browser cache

The JS/CSS files are loaded with a `?v=N` parameter in `index.html`. If you later edit `ble.js`/`api.js`/`app.js`/`style.css` yourself, bump that number, or do a full reload in a new tab or private window, to make sure the browser doesn't keep the old cached version. A plain Ctrl+F5 isn't always enough, depending on the browser.

## Quick troubleshooting

- **"Failed to fetch" / CORS error mentioning a URL like `file:///.../test-app/192.168.x.x/...`**: the page was opened by double-clicking it (`file://`) instead of being served from `http://localhost` (see "Running the page" above). On a `file://` page, a base URL entered without `http://` is resolved as a local file path, not as a network request. The page shows a red warning in the Connection panel when it detects this case. Serve the folder with `python -m http.server 8080` and reload.
- **"Failed to fetch" / CORS error while the page is served from `http://localhost`**: check that the flashed firmware includes the CORS headers and that the base URL is correct (try the IP directly if `airbox.local` doesn't resolve on your network). Since `v=8` (cache version), typing just an IP/hostname without a scheme (e.g. `192.168.1.171`) also works: `http://` is added automatically.
- **BLE: nothing happens when clicking "Connect over BLE"**: make sure you're on Chrome/Edge, on `http://localhost` (not `file://`), and that Bluetooth is enabled on the device running the browser.
- **401 on REST requests**: the API key in the Connection field doesn't match the firmware's. Reconnect over BLE and click "Copy the key received over BLE into the WiFi field" again.
