// api.js — REST + WebSocket client for Airbox-ESP, plus the shared activity log.
// Every route documented in API.md has a matching function here.

const Log = (() => {
  let el = null;
  function ensure() {
    if (!el) el = document.getElementById('activityLog');
    return el;
  }
  function add(source, level, label, data) {
    const time = new Date().toLocaleTimeString();
    const line = `[${time}] [${source}/${level}] ${label}` +
      (data !== undefined ? ' ' + JSON.stringify(data) : '');
    const node = ensure();
    if (node) {
      node.textContent += line + '\n';
      node.scrollTop = node.scrollHeight;
    }
    (level === 'error' ? console.error : console.log)(line);
  }
  function clear() { const node = ensure(); if (node) node.textContent = ''; }
  return { add, clear };
})();

const Api = (() => {
  // Accepts a URL typed without a scheme (e.g. "192.168.1.171" or "airbox.local")
  // by automatically prefixing "http://". Without this, fetch() treats it as
  // a RELATIVE path resolved against the page itself — on a page opened via
  // file://, it tries to fetch a local file (file:///.../192.168.1.171/...)
  // instead of contacting the device, with a misleading CORS error as a result.
  function normalizeBaseUrl(v) {
    const trimmed = (v || '').trim().replace(/\/$/, '');
    if (!trimmed) return 'http://airbox.local';
    return /^https?:\/\//i.test(trimmed) ? trimmed : 'http://' + trimmed;
  }
  function baseUrl() {
    return normalizeBaseUrl(localStorage.getItem('airbox_base_url') || 'http://airbox.local');
  }
  function apiKey() {
    return localStorage.getItem('airbox_api_key') || '';
  }
  function setBaseUrl(v) { localStorage.setItem('airbox_base_url', normalizeBaseUrl(v)); }
  function setApiKey(v) { localStorage.setItem('airbox_api_key', v); }

  async function request(method, path, body, opts = {}) {
    const url = baseUrl() + path;
    const headers = {};
    if (body !== undefined) headers['Content-Type'] = 'application/json';
    if (!opts.noAuth) headers['X-Api-Key'] = apiKey();

    const started = performance.now();
    let res, text, data;
    try {
      res = await fetch(url, {
        method,
        headers,
        body: body !== undefined ? JSON.stringify(body) : undefined,
      });
    } catch (err) {
      Log.add('rest', 'error', `${method} ${path} (network)`, { error: String(err) });
      throw err;
    }
    text = await res.text();
    try { data = text ? JSON.parse(text) : {}; } catch { data = { raw: text }; }

    const ms = Math.round(performance.now() - started);
    Log.add('rest', res.ok ? 'ok' : 'error', `${method} ${path} -> ${res.status} (${ms}ms)`, data);

    if (!res.ok) {
      const e = new Error(data.error || `HTTP ${res.status}`);
      e.data = data;
      e.status = res.status;
      throw e;
    }
    return data;
  }

  const get = (path, opts) => request('GET', path, undefined, opts);
  const post = (path, body, opts) => request('POST', path, body, opts);
  const put = (path, body, opts) => request('PUT', path, body, opts);
  const del = (path, opts) => request('DELETE', path, undefined, opts);

  // --- System --------------------------------------------------------------
  const System = {
    info: () => get('/api/system/info', { noAuth: true }),
    status: () => get('/api/system/status'),
    reboot: () => post('/api/system/reboot'),
    factoryReset: () => post('/api/system/factory-reset'),
    regenerateKey: () => post('/api/system/regenerate-key'),
    getLanguage: () => get('/api/system/language'),
    setLanguage: (language) => put('/api/system/language', { language }),
    // RAW body (not multipart) — see API.md. XHR rather than fetch() to
    // get upload progress (not easily exposed by fetch).
    ota: (file, onProgress) => new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      const path = '/api/system/ota';
      xhr.open('POST', baseUrl() + path);
      xhr.setRequestHeader('X-Api-Key', apiKey());
      xhr.setRequestHeader('Content-Type', 'application/octet-stream');
      xhr.upload.onprogress = (e) => {
        if (onProgress && e.lengthComputable) onProgress(e.loaded / e.total);
      };
      xhr.onload = () => {
        let data;
        try { data = xhr.responseText ? JSON.parse(xhr.responseText) : {}; }
        catch { data = { raw: xhr.responseText }; }
        Log.add('rest', xhr.status < 400 ? 'ok' : 'error', `POST ${path} -> ${xhr.status}`, data);
        if (xhr.status < 400) { resolve(data); return; }
        const e = new Error(data.error || `HTTP ${xhr.status}`);
        e.data = data;
        reject(e);
      };
      xhr.onerror = () => {
        Log.add('rest', 'error', `POST ${path} (network)`, { error: 'network error' });
        reject(new Error('network error'));
      };
      xhr.send(file);
    }),
  };

  // --- Safety ----------------------------------------------------------------
  // rebreathCapMs/airCapMs are independent: only pass the field(s) to
  // change (undefined = omitted from the body, so unchanged on the firmware).
  const Safety = {
    status: () => get('/api/system/safety-status'),
    arm: () => post('/api/system/arm'),
    disarm: () => post('/api/system/disarm'),
    estop: () => post('/api/system/estop', undefined, { noAuth: true }),
    rearm: () => post('/api/system/rearm'),
    setConfig: (rebreathCapMs, airCapMs) => {
      const body = {};
      if (rebreathCapMs !== undefined) body.rebreathCapMs = rebreathCapMs;
      if (airCapMs !== undefined) body.airCapMs = airCapMs;
      return put('/api/system/safety-config', body);
    },
  };

  // --- WiFi ------------------------------------------------------------------
  const Wifi = {
    status: () => get('/api/wifi/status'),
    scan: () => get('/api/wifi/scan'),
    config: (ssid, password) => post('/api/wifi/config', { ssid, password }),
    forget: () => post('/api/wifi/forget'),
  };

  // --- Relays ------------------------------------------------------------------
  const Relays = {
    list: () => get('/api/relays'),
    get: (name) => get(`/api/relays/${name}`),
    set: (name, state) => post(`/api/relays/${name}/set`, { state }),
    toggle: (name) => post(`/api/relays/${name}/toggle`),
  };

  // --- Display ---------------------------------------------------------------
  const Display = {
    text: (line1, line2, durationMs) => post('/api/display/text', { line1, line2, durationMs }),
  };

  // --- WebSocket ----------------------------------------------------------------
  let ws = null;
  const Ws = {
    connect(onMessage) {
      const wsUrl = baseUrl().replace(/^http/, 'ws') + '/ws?key=' + encodeURIComponent(apiKey());
      ws = new WebSocket(wsUrl);
      ws.onopen = () => Log.add('ws', 'ok', 'connected');
      ws.onclose = () => Log.add('ws', 'info', 'disconnected');
      ws.onerror = (e) => Log.add('ws', 'error', 'error', { error: String(e) });
      ws.onmessage = (ev) => {
        let data;
        try { data = JSON.parse(ev.data); } catch { data = { raw: ev.data }; }
        Log.add('ws', 'notify', 'message', data);
        if (onMessage) onMessage(data);
      };
    },
    disconnect() { if (ws) ws.close(); ws = null; },
    isConnected: () => !!ws && ws.readyState === WebSocket.OPEN,
  };

  return { baseUrl, apiKey, setBaseUrl, setApiKey, System, Safety, Wifi, Relays, Display, Ws };
})();
