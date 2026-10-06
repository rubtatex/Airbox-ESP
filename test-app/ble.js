// ble.js — Web Bluetooth client for the custom "Airbox Control" service.
// Chrome/Edge/Opera only, page served in a secure context
// (http://localhost or https, never file://). See README.md.

const Ble = (() => {
  const SERVICE_UUID = '9b1a0001-8f1a-4a2b-9c3e-1a2b3c4d5e6f';
  const CHAR_CMD = '9b1a0002-8f1a-4a2b-9c3e-1a2b3c4d5e6f';
  const CHAR_STATE = '9b1a0003-8f1a-4a2b-9c3e-1a2b3c4d5e6f';
  const CHAR_ESTOP = '9b1a0004-8f1a-4a2b-9c3e-1a2b3c4d5e6f';
  const CHAR_WIFI_CONFIG = '9b1a0005-8f1a-4a2b-9c3e-1a2b3c4d5e6f';

  let device = null;
  let chars = {};
  let onStateChanged = null;
  // The API key is learned from the STATE characteristic itself (read
  // without authentication) and reused for CMD/WIFI_CONFIG writes.
  // The BLE module is thus fully self-contained: it never depends on the
  // key field of the WiFi/REST panel (Api.apiKey()).
  let knownApiKey = null;

  function isSupported() {
    return !!navigator.bluetooth;
  }

  function encode(obj) {
    return new TextEncoder().encode(JSON.stringify(obj));
  }

  function decode(dataView) {
    const text = new TextDecoder().decode(dataView);
    try { return JSON.parse(text); } catch { return { raw: text }; }
  }

  // Explicit read of STATE (via readValue, NOT a notification). Use it
  // when the payload may exceed the size of a BLE notification
  // (~MTU-3, ~250 bytes) — e.g. the list of scanned WiFi networks. An
  // explicit read has no such limit (multi-chunk blob read handled
  // natively by the Bluetooth stack), unlike notifications, which are
  // silently truncated beyond the MTU. See CLAUDE.md.
  async function readState() {
    if (!chars.state) throw new Error('not connected over BLE');
    const json = decode(await chars.state.readValue());
    if (json.apiKey) knownApiKey = json.apiKey;
    Log.add('ble', 'read', 'STATE', json);
    return json;
  }

  async function connect() {
    if (!isSupported()) throw new Error('Web Bluetooth not supported by this browser (use Chrome/Edge)');

    device = await navigator.bluetooth.requestDevice({
      filters: [{ services: [SERVICE_UUID] }],
    });
    device.addEventListener('gattserverdisconnected', () => {
      Log.add('ble', 'info', 'disconnected');
      chars = {};
    });

    const server = await device.gatt.connect();
    const service = await server.getPrimaryService(SERVICE_UUID);
    chars.cmd = await service.getCharacteristic(CHAR_CMD);
    chars.state = await service.getCharacteristic(CHAR_STATE);
    chars.estop = await service.getCharacteristic(CHAR_ESTOP);
    chars.wifiConfig = await service.getCharacteristic(CHAR_WIFI_CONFIG);

    await chars.state.startNotifications();
    chars.state.addEventListener('characteristicvaluechanged', (ev) => {
      // Notification pushed by the firmware: handy for live monitoring
      // (safety state, relays...), but it MAY be truncated if the payload
      // exceeds the MTU (e.g. when wifiNetworks is present) — never rely
      // on it for large data, use readState().
      const json = decode(ev.target.value);
      if (json.apiKey) knownApiKey = json.apiKey;
      Log.add('ble', 'notify', 'STATE', json);
      if (onStateChanged) onStateChanged(json);
    });

    const initial = await readState();
    if (onStateChanged) onStateChanged(initial);
    return initial;
  }

  function disconnect() {
    if (device && device.gatt.connected) device.gatt.disconnect();
    chars = {};
    knownApiKey = null;
  }

  function isConnected() {
    return !!(device && device.gatt && device.gatt.connected);
  }

  async function writeCmd(cmdObj) {
    if (!chars.cmd) throw new Error('not connected over BLE');
    const payload = { key: knownApiKey || '', ...cmdObj };
    await chars.cmd.writeValue(encode(payload));
    Log.add('ble', 'write', 'CMD', payload);
  }

  async function writeEstop() {
    if (!chars.estop) throw new Error('not connected over BLE');
    // Any value triggers the emergency stop — no API key needed.
    await chars.estop.writeValue(new Uint8Array([1]));
    Log.add('ble', 'write', 'ESTOP', {});
  }

  async function writeWifiConfig(ssid, password) {
    if (!chars.wifiConfig) throw new Error('not connected over BLE');
    const payload = { key: knownApiKey || '', ssid, password };
    await chars.wifiConfig.writeValue(encode(payload));
    Log.add('ble', 'write', 'WIFI_CONFIG', { ssid });
  }

  return {
    isSupported,
    connect,
    disconnect,
    isConnected,
    readState,
    writeCmd,
    writeEstop,
    writeWifiConfig,
    getKnownApiKey: () => knownApiKey,
    onState: (cb) => { onStateChanged = cb; },
  };
})();
