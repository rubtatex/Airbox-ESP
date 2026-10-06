// app.js — glue: wires the index.html buttons to api.js/ble.js and
// updates the display areas (safety state, JSON outputs, log).

function $(id) { return document.getElementById(id); }

function showOutput(elId, data) {
  $(elId).textContent = typeof data === 'string' ? data : JSON.stringify(data, null, 2);
}

function showError(elId, err) {
  $(elId).textContent = 'Error: ' + (err && err.message ? err.message : String(err));
}

// Runs an async action, shows the result/error in outputElId
// (the full details are in the log anyway, via api.js/ble.js).
async function run(outputElId, fn) {
  try {
    const result = await fn();
    if (outputElId) showOutput(outputElId, result ?? { ok: true });
    return result;
  } catch (err) {
    if (outputElId) showError(outputElId, err);
    throw err;
  }
}

// ---------------------------------------------------------------------------
// Safety state (shared badge, fed by REST / WS / BLE)
// ---------------------------------------------------------------------------

function renderSafety(obj) {
  if (!obj) return;
  const state = obj.safetyState || obj.state;
  if (state) {
    const badge = $('safetyStateBadge');
    badge.textContent = state.toUpperCase();
    badge.className = 'badge badge-' + state;
  }
  if (obj.rebreathRemainingMs !== undefined) {
    $('rebreathCountdown').textContent =
      obj.rebreathRemainingMs > 0 ? ` — Re-Breath closes in ${Math.ceil(obj.rebreathRemainingMs / 1000)}s` : '';
  }
  if (obj.airRemainingMs !== undefined) {
    $('airCountdown').textContent =
      obj.airRemainingMs > 0 ? ` — Air reopens in ${Math.ceil(obj.airRemainingMs / 1000)}s` : '';
  }
}

// label/energized independent of the source format (REST provides both,
// BLE only provides a logical "open"/"running" boolean — energized omitted then).
function renderRelayState(name, label, energized) {
  const card = document.querySelector(`.relay-card[data-relay="${name}"]`);
  if (!card) return;
  card.querySelector('.relay-label').textContent =
    energized === undefined ? label : `${label} (energized=${energized})`;
}

function renderRelay(relayObj) {
  if (!relayObj || !relayObj.name) return;
  renderRelayState(relayObj.name, relayObj.label, relayObj.energized);
}

// s: BLE STATE object (see API.md) — {air:{open}, rebreath:{open}, aroma:{running}}.
// No "label"/"energized" like in REST, so we rebuild the logical label
// ourselves (same labels as RelayController on the firmware side).
function renderRelaysFromBleState(s) {
  if (s.air) renderRelayState('air', s.air.open ? 'OPEN' : 'CLOSED');
  if (s.rebreath) renderRelayState('rebreath', s.rebreath.open ? 'OPEN' : 'CLOSED');
  if (s.aroma) renderRelayState('aroma', s.aroma.running ? 'RUNNING' : 'STOPPED');
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

$('baseUrl').value = Api.baseUrl();
$('apiKey').value = Api.apiKey();

// Web Bluetooth AND the REST calls assume a real http:// server.
// A page opened by double-click (file://) silently breaks both
// (the entered base URL is then resolved as a local file path
// instead of a network request) — see README "Quick troubleshooting".
if (location.protocol === 'file:') {
  const msg = 'Page opened via file:// — serve this folder over http://localhost (see README), otherwise BLE and REST will fail.';
  $('connectionStatus').textContent = msg;
  Log.add('app', 'error', msg);
}

// Periodic REST refresh starts ONLY on explicit action
// ("Save and connect" button) — never automatically on page
// load, to avoid spamming the log with failed calls
// until WiFi has been deliberately configured/tested.
let safetyPollTimer = null;

// buildStatusJson (REST/WS) does not include relay state (see API.md) —
// so relays must be refreshed separately via /api/relays, in addition to
// the safety status, so the cards don't stay stuck on "?".
function refreshRelaysRest() {
  Api.Relays.list().then((list) => list.forEach(renderRelay)).catch(() => {});
}

function startSafetyPolling() {
  stopSafetyPolling();
  safetyPollTimer = setInterval(() => {
    Api.Safety.status().then(renderSafety).catch(() => {});
    refreshRelaysRest();
  }, 4000);
  Api.Safety.status().then(renderSafety).catch(() => {});
  refreshRelaysRest();
}

function stopSafetyPolling() {
  if (safetyPollTimer) clearInterval(safetyPollTimer);
  safetyPollTimer = null;
}

$('btnSaveConnection').onclick = () => {
  Api.setBaseUrl($('baseUrl').value.trim() || 'http://airbox.local');
  Api.setApiKey($('apiKey').value.trim());
  $('connectionStatus').textContent = 'Saved, refreshing every 4s.';
  startSafetyPolling();
};

$('btnDisconnectWifi').onclick = () => {
  stopSafetyPolling();
  $('connectionStatus').textContent = 'Refresh stopped.';
};

// ---------------------------------------------------------------------------
// Safety
// ---------------------------------------------------------------------------

$('btnArm').onclick = () => run(null, Api.Safety.arm).then(renderSafety);
$('btnDisarm').onclick = () => run(null, Api.Safety.disarm).then(renderSafety);
$('btnRearm').onclick = () => run(null, Api.Safety.rearm).then(renderSafety);
$('btnEstop').onclick = () => run(null, Api.Safety.estop).then(renderSafety);
$('btnSafetyStatus').onclick = () => run(null, Api.Safety.status).then(renderSafety);
$('btnSetSafetyConfig').onclick = () => {
  // Each field is optional/independent: empty = leave that cap unchanged.
  const rb = $('rebreathCapMs').value.trim();
  const air = $('airCapMs').value.trim();
  run(null, () => Api.Safety.setConfig(
    rb ? parseInt(rb, 10) : undefined,
    air ? parseInt(air, 10) : undefined,
  ));
};

// ---------------------------------------------------------------------------
// System
// ---------------------------------------------------------------------------

$('btnSystemInfo').onclick = () => run('systemOutput', Api.System.info);
$('btnSystemStatus').onclick = () => run('systemOutput', Api.System.status).then(renderSafety);
$('btnReboot').onclick = () => {
  if (confirm('Reboot the device?')) run('systemOutput', Api.System.reboot);
};
$('btnFactoryReset').onclick = () => {
  if (confirm('WARNING: erases WiFi/API key/safety config and reboots. Confirm?')) {
    run('systemOutput', Api.System.factoryReset);
  }
};
$('btnRegenerateKey').onclick = async () => {
  if (!confirm('Regenerate the API key: the old one will stop working immediately. Confirm?')) return;
  const res = await run('systemOutput', Api.System.regenerateKey);
  if (res && res.apiKey) {
    $('apiKey').value = res.apiKey;
    Api.setApiKey(res.apiKey);
  }
};
$('btnWifiForget').onclick = () => {
  if (confirm('Forget WiFi and switch back to fallback access point mode (100% local)? The API key is kept.')) {
    run('systemOutput', Api.Wifi.forget);
  }
};

$('btnGetLanguage').onclick = async () => {
  const res = await run('systemOutput', Api.System.getLanguage);
  if (res && res.language) $('languageSelect').value = res.language;
};
$('btnSetLanguage').onclick = () => run('systemOutput', () => Api.System.setLanguage($('languageSelect').value));

// ---------------------------------------------------------------------------
// OTA (firmware update)
// ---------------------------------------------------------------------------

$('btnOtaUpload').onclick = async () => {
  const file = $('otaFile').files[0];
  if (!file) { alert('Choose a .bin file (see .pio/build/.../firmware.bin after a pio run).'); return; }
  if (!confirm(`Flash "${file.name}" (${Math.round(file.size / 1024)} KB)? The system must be DISARMED; the device will reboot after a successful flash.`)) return;

  $('otaProgress').value = 0;
  $('otaStatus').textContent = 'Uploading...';
  $('btnOtaUpload').disabled = true;
  try {
    await Api.System.ota(file, (frac) => { $('otaProgress').value = Math.round(frac * 100); });
    $('otaStatus').textContent = 'Flash applied, device rebooting...';
    stopSafetyPolling();
  } catch (err) {
    $('otaStatus').textContent = 'Error: ' + err.message;
  } finally {
    $('btnOtaUpload').disabled = false;
  }
};

// ---------------------------------------------------------------------------
// WiFi — graphical network list (signal bars, secured/open,
// one-click connect) shared between the WiFi panel (REST, enriched
// data) and the BLE panel (SSID only, see renderNetworkList).
// ---------------------------------------------------------------------------

// filled: -1 = unknown (no RSSI, BLE case), 0-4 = number of filled bars.
function signalBarsHtml(rssi) {
  let filled;
  if (rssi === undefined || rssi === null) filled = -1;
  else if (rssi >= -55) filled = 4;
  else if (rssi >= -65) filled = 3;
  else if (rssi >= -75) filled = 2;
  else if (rssi >= -85) filled = 1;
  else filled = 0;

  let html = '<span class="signal-bars" title="' + (filled < 0 ? 'unknown signal' : rssi + ' dBm') + '">';
  for (let i = 1; i <= 4; i++) {
    html += `<span class="bar${filled >= i ? ' filled' : ''}"></span>`;
  }
  return html + '</span>';
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
  }[c]));
}

// networks: array of Strings (SSID only, BLE case) or of objects
// {ssid, rssi, secure, connected} (REST case, see GET /api/wifi/scan).
// onConnect(ssid, password) is called when the user confirms.
function renderNetworkList(container, networks, onConnect) {
  container.innerHTML = '';
  if (!networks || networks.length === 0) {
    container.innerHTML = '<p class="hint">No networks found.</p>';
    return;
  }

  networks.forEach((net) => {
    const isRich = typeof net === 'object';
    const ssid = isRich ? net.ssid : net;
    const rssi = isRich ? net.rssi : null;
    // null = unknown (the BLE scan returns {ssid, rssi} but not "secure",
    // to stay under the STATE characteristic size limit —
    // see CLAUDE.md). typeof-check rather than `!= null` so as not to
    // confuse "absent" (BLE) with "false" (open network, REST).
    const secure = isRich && typeof net.secure === 'boolean' ? net.secure : null;
    const connected = isRich && !!net.connected;

    const card = document.createElement('div');
    card.className = 'network-card' + (connected ? ' connected' : '');
    card.innerHTML = `
      <div class="network-info">
        ${signalBarsHtml(rssi)}
        <span class="network-name">${escapeHtml(ssid)}</span>
        ${secure === null ? '' : `<span class="network-security">${secure ? 'secured' : 'open'}</span>`}
        ${connected ? '<span class="network-connected-badge">connected</span>' : ''}
        <button class="btn-network-toggle">${connected ? 'Reconnect' : 'Connect'}</button>
      </div>
      <div class="network-connect-row" hidden>
        <input type="password" class="network-password" placeholder="Password">
        <button class="btn-network-confirm">Connect</button>
        <button class="btn-network-cancel">Cancel</button>
      </div>`;

    const row = card.querySelector('.network-connect-row');
    const pwInput = card.querySelector('.network-password');

    card.querySelector('.btn-network-toggle').onclick = () => {
      if (secure === false) {
        // Open network: no password needed, connect directly.
        onConnect(ssid, '');
      } else {
        row.hidden = !row.hidden;
        if (!row.hidden) pwInput.focus();
      }
    };
    card.querySelector('.btn-network-confirm').onclick = () => onConnect(ssid, pwInput.value);
    card.querySelector('.btn-network-cancel').onclick = () => { row.hidden = true; };

    container.appendChild(card);
  });
}

function renderWifiStatus(res) {
  if (!res) return;
  $('wifiConnectedSsid').textContent = res.connected
    ? res.ssid
    : (res.apMode ? '(access point mode)' : 'not connected');
}

$('btnWifiStatus').onclick = () => run('wifiOutput', Api.Wifi.status).then(renderWifiStatus);

$('btnWifiScan').onclick = async () => {
  showOutput('wifiOutput', 'Scanning...');
  for (let i = 0; i < 10; i++) {
    const res = await run('wifiOutput', Api.Wifi.scan);
    if (res.networks) {
      renderNetworkList($('wifiNetworksList'), res.networks, connectToWifiRest);
      showOutput('wifiOutput', { networksFound: res.networks.length });
      return;
    }
    await new Promise((r) => setTimeout(r, 1000));
  }
};

async function connectToWifiRest(ssid, password) {
  try {
    await run('wifiOutput', () => Api.Wifi.config(ssid, password));
    $('wifiSsid').value = ssid;
  } catch (err) {
    alert('WiFi connection error: ' + err.message);
  }
}

$('btnWifiConfig').onclick = () => {
  run('wifiOutput', () => Api.Wifi.config($('wifiSsid').value.trim(), $('wifiPassword').value));
};

// ---------------------------------------------------------------------------
// Relays
// ---------------------------------------------------------------------------

document.querySelectorAll('.relay-card').forEach((card) => {
  const name = card.dataset.relay;
  card.querySelector('.btn-relay-refresh').onclick = () =>
    Api.Relays.get(name).then(renderRelay).catch((e) => alert(e.message));
  card.querySelector('.btn-relay-on').onclick = () =>
    Api.Relays.set(name, true).then(() => Api.Relays.get(name)).then(renderRelay).catch((e) => alert(e.message));
  card.querySelector('.btn-relay-off').onclick = () =>
    Api.Relays.set(name, false).then(() => Api.Relays.get(name)).then(renderRelay).catch((e) => alert(e.message));
  card.querySelector('.btn-relay-toggle').onclick = () =>
    Api.Relays.toggle(name).then(() => Api.Relays.get(name)).then(renderRelay).catch((e) => alert(e.message));
});

$('btnRelaysList').onclick = () => Api.Relays.list().then((list) => list.forEach(renderRelay));

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

$('btnDisplayText').onclick = () => {
  run(null, () => Api.Display.text(
    $('displayLine1').value, $('displayLine2').value, parseInt($('displayDuration').value, 10) || 3000
  ));
};

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------

$('btnWsConnect').onclick = () => {
  Api.Ws.connect((data) => {
    showOutput('wsOutput', data);
    renderSafety(data);
    // The WS message does not include relay state (see API.md) — on an
    // event that may change one, fetch it explicitly.
    if (data.event === 'relay_changed' || data.event === 'safety_cap_triggered' ||
        data.event === 'estop_triggered' || data.event === 'disarmed') {
      refreshRelaysRest();
    }
  });
  $('wsStatus').textContent = 'connecting...';
  setTimeout(() => {
    $('wsStatus').textContent = Api.Ws.isConnected() ? 'connected' : 'connection failed';
  }, 1000);
};
$('btnWsDisconnect').onclick = () => {
  Api.Ws.disconnect();
  $('wsStatus').textContent = 'disconnected';
};

// ---------------------------------------------------------------------------
// BLE
// ---------------------------------------------------------------------------

if (!Ble.isSupported()) {
  $('bleStatus').textContent = 'Web Bluetooth not supported by this browser (use Chrome/Edge)';
}

// Last BLE state received (initial read or notification) — the BLE
// connection stays entirely separate from the WiFi/REST config above; only
// the "Copy the key received over BLE into the WiFi field" button bridges
// the two, explicitly.
let lastBleState = null;

function onBleState(s) {
  lastBleState = s;
  showOutput('bleStateOutput', s);
  renderSafety(s);
  renderRelaysFromBleState(s);
  if (Array.isArray(s.wifiNetworks)) renderBleWifiResults(s.wifiNetworks);
}

$('btnBleConnect').onclick = async () => {
  try {
    $('bleStatus').textContent = 'connecting...';
    const state = await Ble.connect();
    Ble.onState(onBleState);
    onBleState(state);
    $('bleStatus').textContent = 'connected';
  } catch (err) {
    $('bleStatus').textContent = 'error: ' + err.message;
  }
};

$('btnBleDisconnect').onclick = () => {
  Ble.disconnect();
  $('bleStatus').textContent = 'disconnected';
  lastBleState = null;
};

$('btnCopyKeyFromBle').onclick = () => {
  if (!lastBleState || !lastBleState.apiKey) {
    alert('Connect over BLE first ("Connect over BLE" button above).');
    return;
  }
  $('apiKey').value = lastBleState.apiKey;
  Api.setApiKey(lastBleState.apiKey);
  $('connectionStatus').textContent = 'Key copied from the BLE connection.';
};

$('btnBleArm').onclick = () => Ble.writeCmd({ cmd: 'arm' }).catch((e) => alert(e.message));
$('btnBleDisarm').onclick = () => Ble.writeCmd({ cmd: 'disarm' }).catch((e) => alert(e.message));
$('btnBleRearm').onclick = () => Ble.writeCmd({ cmd: 'rearm' }).catch((e) => alert(e.message));
$('btnBleEstop').onclick = () => Ble.writeEstop().catch((e) => alert(e.message));

$('btnBleRelayOn').onclick = () =>
  Ble.writeCmd({ cmd: 'relay.set', relay: $('bleRelaySelect').value, state: true }).catch((e) => alert(e.message));
$('btnBleRelayOff').onclick = () =>
  Ble.writeCmd({ cmd: 'relay.set', relay: $('bleRelaySelect').value, state: false }).catch((e) => alert(e.message));
$('btnBleRelayToggle').onclick = () =>
  Ble.writeCmd({ cmd: 'relay.toggle', relay: $('bleRelaySelect').value }).catch((e) => alert(e.message));

$('btnBleSafetyConfig').onclick = () => {
  // Each field is optional/independent: only include the ones filled in.
  const payload = { cmd: 'safety.config' };
  const rb = $('bleRebreathCapMs').value.trim();
  const air = $('bleAirCapMs').value.trim();
  if (rb) payload.rebreathCapMs = parseInt(rb, 10);
  if (air) payload.airCapMs = parseInt(air, 10);
  Ble.writeCmd(payload).catch((e) => alert(e.message));
};

$('btnBleDisplayText').onclick = () => {
  Ble.writeCmd({
    cmd: 'display.text',
    line1: $('bleDisplayLine1').value,
    line2: $('bleDisplayLine2').value,
    durationMs: parseInt($('bleDisplayDuration').value, 10) || 3000,
  }).catch((e) => alert(e.message));
};

$('btnBleSetLanguage').onclick = () =>
  Ble.writeCmd({ cmd: 'system.language', language: $('bleLanguageSelect').value }).catch((e) => alert(e.message));

$('btnBleWifiConfig').onclick = () =>
  Ble.writeWifiConfig($('bleWifiSsid').value.trim(), $('bleWifiPassword').value).catch((e) => alert(e.message));

$('btnBleWifiForget').onclick = () => {
  if (!confirm('Forget WiFi (BLE) and switch back to fallback access point mode?')) return;
  Ble.writeCmd({ cmd: 'wifi.forget' }).catch((e) => alert(e.message));
};

$('btnBleWifiScan').onclick = async () => {
  try {
    $('bleWifiResults').innerHTML = '<p class="hint">Scanning...</p>';
    await Ble.writeCmd({ cmd: 'wifi.scan' });
    // Poll actively (readState) rather than relying on notifications: the
    // network list can exceed the max size of a BLE notification and would
    // be silently truncated there. See ble.js.
    for (let i = 0; i < 10; i++) {
      await new Promise((r) => setTimeout(r, 1000));
      const state = await Ble.readState();
      onBleState(state);
      if (!state.wifiScanning && Array.isArray(state.wifiNetworks)) return;
    }
    $('bleWifiResults').innerHTML = '<p class="hint">No result (timeout).</p>';
  } catch (err) {
    alert(err.message);
  }
};

function renderBleWifiResults(networks) {
  renderNetworkList($('bleWifiResults'), networks, (ssid, password) => {
    Ble.writeWifiConfig(ssid, password).catch((e) => alert(e.message));
    $('bleWifiSsid').value = ssid;
  });
}

// ---------------------------------------------------------------------------
// Log
// ---------------------------------------------------------------------------

$('btnClearLog').onclick = () => Log.clear();
