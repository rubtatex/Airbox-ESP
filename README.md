# Airbox-ESP

Firmware for a small breath-control device. It runs on a **Seeed XIAO ESP32-C3** and drives two valves and an aroma pump through a relay board. You control it from a phone or browser over **Wi-Fi (REST + WebSocket)** or **Bluetooth LE**, and a local OLED screen shows its state.

> [!WARNING]
> **This is not a medical device.** It is a hobby/experimental project that controls airflow to a person. It has not been certified, audited or clinically validated. Use it at your own risk, never unattended, and never as the only thing keeping someone safe. See [Safety design](#safety-design) and [Known limitations](#known-limitations).

## Features

- **3 relay outputs**: Air valve (normally open), Re-Breath valve (normally closed), Aroma pump.
- **Fail-safe by design**: with a relay coil de-energized (idle, boot, crash, watchdog reset, power loss), Air is open, Re-Breath is closed and the pump is off.
- **Safety state machine**: `DISARMED` → `ARMED` → `ESTOP`. Nothing can be energized unless the device is armed. It always boots disarmed.
- **Emergency stop** from the physical button, REST or BLE. It needs no API key, so a lost key can never block it.
- **Hard time caps** on Re-Breath open and Air closed (default 120 s, configurable 5–300 s each). The firmware enforces them whatever the command source.
- **Hardware watchdog**: the main loop never blocks; if it hangs, the chip resets into the safe state.
- **REST API + WebSocket** (live state push) over Wi-Fi, with a per-device API key.
- **BLE GATT service** with the same controls, so it works with no Wi-Fi at all.
- **Wi-Fi provisioning**: the device falls back to an open access point (`Airbox-XXXXXX`) and shows a QR code to join it. You can configure Wi-Fi over REST or BLE.
- **OTA firmware updates** over Wi-Fi (only while disarmed).
- **1.3" OLED status screen** (SH1106): safety state, relay states, cap countdown bars, connection QR code. Screen text is in English or French, switchable through the API.
- mDNS: `http://airbox.local`.

## Hardware

| Function | XIAO pin | GPIO |
|---|---|---|
| OLED SDA (SH1106, I2C) | D4 | GPIO6 |
| OLED SCL | D5 | GPIO7 |
| Relay 1: Air valve (NO) | D1 | GPIO3 |
| Relay 2: Re-Breath valve (NC) | D2 | GPIO4 |
| Relay 3: Aroma pump | D3 | GPIO5 |
| BOOT button (on board) | D9 | GPIO9 |

- Board: Seeed Studio XIAO ESP32-C3 (4 MB flash).
- Relay board: 4-channel module, assumed **active LOW** (`RELAY_ACTIVE_LOW` in [src/Config.h](src/Config.h)). Its inputs must have pull-ups so the relays stay off while the ESP32 pins float during boot or flashing.
- Display: 1.3" 128x64 I2C OLED, SH1106 controller, usually at address `0x3C`, powered at 3.3 V.
- Strapping pins (GPIO2/8/9) are deliberately not used as outputs.

**BOOT button**
- Long press (≥ 1 s): emergency stop, or re-arm when already in ESTOP.
- Short press: briefly shows network info, then the API key, on the screen.

## Building and flashing

You need [PlatformIO](https://platformio.org/) (CLI or the VS Code extension).

```sh
pio run                 # build
pio run -t upload       # flash over USB
pio device monitor      # serial monitor, 115200 baud (prints the API key at boot)
```

PlatformIO installs the dependencies listed in [platformio.ini](platformio.ini): U8g2, QRCode, ArduinoJson, NimBLE-Arduino, ESPAsyncWebServer and AsyncTCP (ESP32Async forks).

## First setup

1. Power the device. It starts `DISARMED`.
2. Get the API key, using one of these:
   - **BLE**: connect to `Airbox` and read the `STATE` characteristic. It contains `apiKey`.
   - **Access point**: if no Wi-Fi is configured, join `Airbox-XXXXXX` (scan the QR code on the screen) and open `http://192.168.4.1/api/system/info`.
   - **Screen**: press BOOT briefly twice.
   - **Serial**: the key is printed at boot.
3. Optionally configure Wi-Fi: `POST /api/wifi/config`, or the `WIFI_CONFIG` BLE characteristic.
4. Arm the system (`POST /api/system/arm`), then control the relays.

## Documentation

- **[API.md](API.md)**: full REST, WebSocket and BLE reference, safety model, and app guidelines. It is self-contained and written for mobile app developers.
- **[test-app/](test-app/)**: a static, dependency-free web page for exercising every feature over Wi-Fi and Web Bluetooth. Serve it from `localhost` (`python -m http.server 8080`); see its [README](test-app/README.md).
- **[.claude/CLAUDE.md](.claude/CLAUDE.md)**: detailed design notes for contributors: architecture, safety rationale, design decisions and their history, and pitfalls already hit.

## Safety design

- **Single gatekeeper**: every relay change, from REST, BLE, the button or the screen, goes through `SafetyManager`. No other code touches the relay GPIOs.
- **De-energizing is always allowed**; energizing requires `ARMED`.
- **ESTOP** forces all relays off immediately. Leaving it requires an explicit re-arm.
- **Time caps** cut Re-Breath (open) and Air (closed) automatically, and never re-open them on their own.
- **Watchdog** (~4 s): a firmware hang resets the chip into the safe, disarmed state.
- **OTA is refused unless disarmed**. A failed or interrupted upload never touches the running firmware.

## Known limitations

- **No independent hardware kill switch.** Safety relies on the firmware, the watchdog and the NO/NC valve mechanics. A physical emergency stop that cuts relay power independently of the ESP32 would be the most valuable addition.
- **Sequences run on the app side.** The firmware runs no multi-step automations. If the phone loses its connection mid-sequence, the sequence stops; the safety caps still apply.
- **The OLED display path has not yet been verified on real hardware.** It builds and flashes, but has not been checked on a wired unit.
- Relay polarity (active LOW) and the relay board's pull-ups must be checked on your own hardware before use.
- No real-time clock; nothing depends on absolute time.
- No automatic bootloader rollback after OTA. Test new images over USB first.

## License

Released under the [MIT License](LICENSE). As the license states, the software is provided "as is", without warranty of any kind; see also the warning at the top of this file.
