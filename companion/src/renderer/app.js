/* global WebSocket */

const logEl = document.getElementById("log");
const connectBtn = document.getElementById("connect");
const disconnectBtn = document.getElementById("disconnect");
const scanBtn = document.getElementById("scan");
const devicesSelect = document.getElementById("devices");
const hostInput = document.getElementById("host");
const portInput = document.getElementById("port");

const RECONNECT_DELAY_MS = 3000;
const PING_INTERVAL_MS = 20000;
const VOLUME_SETTLE_MS = 180;

const ACTIONS = ["volume_up", "volume_down", "mute", "play_pause", "next", "previous", "app"];
const ICONS = [
  "vol_up", "vol_down", "mute", "play", "pause", "next", "prev", "shuffle", "power", "settings",
  "home", "bell", "mail", "wifi", "file", "folder", "gpt", "codex", "cursor", "iterm", "terminal",
  "vscode", "slack", "telegram", "safari", "chrome", "finder", "music", "messages", "app",
];
const COLORS = [
  "#475569", "#64748B", "#0F766E", "#0369A1", "#BE123C", "#16A34A", "#7C3AED", "#10A37F",
  "#6366F1", "#1F2937", "#007ACC", "#4A154B", "#229ED9",
];
const PRESETS = [
  { name: "ChatGPT", icon: "gpt", color: "#10A37F", value: "com.openai.chat" },
  { name: "Codex", icon: "codex", color: "#0D8A6A", value: "com.openai.codex" },
  { name: "Cursor", icon: "cursor", color: "#6366F1", value: "com.todesktop.230313mzl4w4u92" },
  { name: "iTerm", icon: "iterm", color: "#1F2937", value: "com.googlecode.iterm2" },
  { name: "VS Code", icon: "vscode", color: "#007ACC", value: "com.microsoft.VSCode" },
  { name: "Slack", icon: "slack", color: "#4A154B", value: "com.tinyspeck.slackmacgap" },
  { name: "Telegram", icon: "telegram", color: "#229ED9", value: "ru.keepcoder.Telegram" },
  { name: "Safari", icon: "safari", color: "#0369A1", value: "com.apple.Safari" },
  { name: "Terminal", icon: "terminal", color: "#475569", value: "com.apple.Terminal" },
  { name: "Finder", icon: "finder", color: "#0EA5E9", value: "com.apple.finder" },
  { name: "Messages", icon: "messages", color: "#16A34A", value: "com.apple.MobileSMS" },
  { name: "Music", icon: "music", color: "#BE123C", value: "com.apple.Music" },
];

let socket = null;
let reconnectTimer = null;
let pingTimer = null;
let wantConnection = false;
let discovered = [];
let grid = { rev: 1, cols: 4, rows: 2, tiles: [] };
let sdIcons = [];
let activeApprovals = new Map();

function log(msg, cls = "info") {
  const line = document.createElement("div");
  line.className = cls;
  line.textContent = `[${new Date().toLocaleTimeString()}] ${msg}`;
  logEl.appendChild(line);
  logEl.scrollTop = logEl.scrollHeight;
}

function setMsg(el, text, ok) {
  el.textContent = text;
  el.className = "msg " + (ok ? "ok" : "err");
}

function boardHost() {
  const host = hostInput.value.trim();
  if (!host) throw new Error("Set board host first");
  return host;
}

async function boardJson(path, options = {}) {
  const response = await window.touchdeck.boardRequest({
    host: boardHost(),
    path,
    method: options.method || "GET",
    body: options.body || "",
  });
  let json = {};
  if (response.text) {
    try {
      json = JSON.parse(response.text);
    } catch {
      throw new Error(`Invalid board response (HTTP ${response.status})`);
    }
  }
  if (!response.ok) {
    throw new Error(json.error || response.error || `HTTP ${response.status}`);
  }
  return json;
}

function stopTimers() {
  if (reconnectTimer) {
    clearTimeout(reconnectTimer);
    reconnectTimer = null;
  }
  if (pingTimer) {
    clearInterval(pingTimer);
    pingTimer = null;
  }
}

window.touchdeck.onHostVolume((v) => {
  if (socket?.readyState !== WebSocket.OPEN) return;
  socket.send(JSON.stringify({ op: "volume", level: v.level, muted: v.muted }));
});

function sendDeckNotification(item) {
  if (socket?.readyState !== WebSocket.OPEN) return;
  socket.send(
    JSON.stringify({
      op: "notification",
      id: item.id,
      source: item.source,
      title: item.title,
      body: item.body,
    }),
  );
}

function clearDeckNotification(id) {
  if (socket?.readyState !== WebSocket.OPEN) return;
  socket.send(JSON.stringify({ op: "notification_clear", id }));
}

function clearAllDeckNotifications() {
  if (socket?.readyState !== WebSocket.OPEN) return;
  socket.send(JSON.stringify({ op: "notification_clear_all" }));
}

function syncApprovalsToDeck(pending) {
  const next = new Map();
  for (const item of pending || []) {
    next.set(item.id, item);
  }
  for (const id of activeApprovals.keys()) {
    if (!next.has(id)) {
      clearDeckNotification(id);
    }
  }
  for (const item of next.values()) {
    const prev = activeApprovals.get(item.id);
    if (!prev || prev.body !== item.body) {
      sendDeckNotification(item);
      if (!prev) {
        log(`Approval pending: ${item.title} — ${item.body}`, "ok");
      }
    }
  }
  if (next.size === 0 && activeApprovals.size > 0) {
    clearAllDeckNotifications();
  }
  activeApprovals = next;
}

window.touchdeck.onApprovalUpdate(({ pending }) => {
  syncApprovalsToDeck(pending || []);
});

function scheduleReconnect() {
  if (!wantConnection || reconnectTimer) return;
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    openSocket();
  }, RECONNECT_DELAY_MS);
}

function fillDevices(devices) {
  discovered = devices || [];
  devicesSelect.innerHTML = "";
  if (!discovered.length) {
    const opt = document.createElement("option");
    opt.value = "";
    opt.textContent = "No devices found";
    devicesSelect.appendChild(opt);
    return;
  }
  discovered.forEach((d, i) => {
    const opt = document.createElement("option");
    opt.value = String(i);
    opt.textContent = `${d.name} — ${d.host}:${d.port}`;
    devicesSelect.appendChild(opt);
  });
  applyDevice(0);
}

function applyDevice(index) {
  const d = discovered[index];
  if (!d) return;
  hostInput.value = d.host;
  portInput.value = String(d.port);
  localStorage.setItem("host", d.host);
  localStorage.setItem("port", String(d.port));
}

async function scanDevices({ autoConnect = false } = {}) {
  scanBtn.disabled = true;
  devicesSelect.innerHTML = "";
  const loading = document.createElement("option");
  loading.value = "";
  loading.textContent = "Scanning Bonjour…";
  devicesSelect.appendChild(loading);
  log("Scanning for _touchdeck._tcp on the LAN…");

  const result = await window.touchdeck.discoverDevices();
  scanBtn.disabled = false;

  if (!result.ok) {
    log(`Scan failed: ${result.error}`, "err");
    fillDevices([]);
    return [];
  }

  fillDevices(result.devices);
  if (result.devices.length === 0) {
    log("No TouchDeck found. Keep the board on the same Wi-Fi, then Scan again.", "err");
    return [];
  }

  result.devices.forEach((d) => log(`Found ${d.name} at ${d.host}:${d.port}`, "ok"));
  if (autoConnect && !wantConnection) connect();
  return result.devices;
}

async function handleMessage(text) {
  let data;
  try {
    data = JSON.parse(text);
  } catch {
    log(`Ignored non-JSON message: ${text}`, "err");
    return;
  }

  if (data.type === "hello") {
    log(`Board ${data.model} fw ${data.fw} (protocol ${data.protocol})`, "ok");
    return;
  }
  if (data.type === "pong") return;
  if (data.type === "media_press") {
    // `handled` means the board already sent BLE/USB HID (HUD path).
    if (data.handled) {
      setTimeout(() => window.touchdeck.refreshVolume(), VOLUME_SETTLE_MS);
      return;
    }
    if (data.action === "volume_up" || data.action === "volume_down") {
      // Wi-Fi fallback: exact 3% step, no macOS HUD.
      const delta = data.action === "volume_up" ? 3 : -3;
      const result = await window.touchdeck.adjustVolume(delta);
      if (!result.ok) log(`Fine volume adjustment failed: ${result.error}`, "err");
      else log(`Volume ${delta > 0 ? "+" : ""}${delta}% (Wi-Fi, no HUD)`, "ok");
      return;
    }
    log(`Media key ${data.action} requires a connected Bluetooth HID host`, "err");
    return;
  }
  if (data.type !== "tile_press") {
    log(`Event: ${text}`);
    return;
  }

  const target = data.target;
  if (!target || !target.kind || !target.value) {
    log(`Tile ${data.id} has no launch target`, "err");
    return;
  }
  const result = await window.touchdeck.launchApp({ kind: target.kind, value: target.value });
  if (result.ok) {
    log(`Launched ${target.kind}:${target.value} (tile ${data.id})`, "ok");
  } else {
    log(`Launch failed: ${result.error}`, "err");
  }
}

function openSocket() {
  const host = hostInput.value.trim();
  const port = portInput.value.trim();
  if (!host || !port) {
    log("Host and port are required", "err");
    return;
  }

  const url = `ws://${host}:${port}/`;
  log(`Connecting to ${url}…`);
  try {
    socket = new WebSocket(url);
  } catch (err) {
    log(String(err), "err");
    scheduleReconnect();
    return;
  }

  socket.addEventListener("open", () => {
    log(`Connected to ${host}`, "ok");
    connectBtn.disabled = true;
    disconnectBtn.disabled = false;
    window.touchdeck.setConnectionStatus(host);
    syncApprovalsToDeck([...activeApprovals.values()]);
    pingTimer = setInterval(() => {
      if (socket?.readyState === WebSocket.OPEN) socket.send('{"op":"ping"}');
    }, PING_INTERVAL_MS);
  });

  socket.addEventListener("message", (ev) => handleMessage(String(ev.data)));
  socket.addEventListener("error", () => {
    log("Socket error — is the board on the same Wi-Fi?", "err");
  });
  socket.addEventListener("close", () => {
    stopTimers();
    socket = null;
    window.touchdeck.setConnectionStatus("Disconnected");
    disconnectBtn.disabled = true;
    connectBtn.disabled = !wantConnection;
    if (wantConnection) {
      log(`Disconnected — retrying in ${RECONNECT_DELAY_MS / 1000}s`, "err");
      scheduleReconnect();
    } else {
      log("Disconnected");
      connectBtn.disabled = false;
    }
  });
}

function connect() {
  wantConnection = true;
  connectBtn.disabled = true;
  openSocket();
}

function disconnect() {
  wantConnection = false;
  stopTimers();
  if (socket) {
    socket.close();
    socket = null;
  }
  connectBtn.disabled = false;
  disconnectBtn.disabled = true;
  window.touchdeck.setConnectionStatus("Disconnected");
}

// ---- Grid editor ----

function el(tag, attrs = {}, kids = []) {
  const n = document.createElement(tag);
  Object.entries(attrs).forEach(([k, v]) => {
    if (k === "text") n.textContent = v;
    else if (k === "html") n.innerHTML = v;
    else n.setAttribute(k, v);
  });
  kids.forEach((c) => n.appendChild(c));
  return n;
}

function fillSelect(sel, from, to, cur) {
  sel.innerHTML = "";
  for (let i = from; i <= to; i++) {
    const o = el("option", { value: String(i), text: String(i) });
    if (i === cur) o.selected = true;
    sel.appendChild(o);
  }
}

function ensureTiles() {
  const n = grid.cols * grid.rows;
  while (grid.tiles.length < n) {
    const i = grid.tiles.length;
    grid.tiles.push({
      id: "tile_" + i,
      label: "Tile " + (i + 1),
      color: COLORS[i % COLORS.length],
      icon: "app",
      action: "app",
      target: { kind: "bundle", value: "com.apple.Safari" },
    });
  }
  grid.tiles = grid.tiles.slice(0, n);
}

function iconOptions(current) {
  const list = [...ICONS];
  sdIcons.forEach((id) => {
    if (!list.includes(id)) list.push(id);
  });
  if (current && !list.includes(current)) list.push(current);
  return list;
}

function moveTile(from, to) {
  if (from === to || from < 0 || to < 0 || from >= grid.tiles.length || to >= grid.tiles.length) {
    return;
  }
  const [item] = grid.tiles.splice(from, 1);
  grid.tiles.splice(to, 0, item);
  renderGrid();
  setMsg(document.getElementById("grid-msg"), `Moved #${from + 1} → #${to + 1} (not saved yet)`, true);
}

function renderGrid() {
  fillSelect(document.getElementById("cols"), 2, 5, grid.cols);
  fillSelect(document.getElementById("rows"), 1, 3, grid.rows);
  ensureTiles();
  const root = document.getElementById("grid");
  root.style.gridTemplateColumns = `repeat(${grid.cols}, minmax(160px, 1fr))`;
  root.innerHTML = "";

  grid.tiles.forEach((t, idx) => {
    const card = el("div", { class: "tile" });
    card.dataset.index = String(idx);

    const head = el("div", { class: "tile-head" });
    const handle = el("span", { class: "tile-handle", text: "⋮⋮", title: "Drag to reorder" });
    handle.draggable = true;
    head.appendChild(handle);
    head.appendChild(el("h3", { text: `#${idx + 1} ${t.id}` }));
    card.appendChild(head);

    handle.addEventListener("dragstart", (e) => {
      e.dataTransfer.effectAllowed = "move";
      e.dataTransfer.setData("text/plain", String(idx));
      card.classList.add("dragging");
    });
    handle.addEventListener("dragend", () => {
      card.classList.remove("dragging");
      root.querySelectorAll(".tile.drag-over").forEach((n) => n.classList.remove("drag-over"));
    });

    card.addEventListener("dragover", (e) => {
      e.preventDefault();
      e.dataTransfer.dropEffect = "move";
      card.classList.add("drag-over");
    });
    card.addEventListener("dragleave", () => card.classList.remove("drag-over"));
    card.addEventListener("drop", (e) => {
      e.preventDefault();
      card.classList.remove("drag-over");
      const from = Number.parseInt(e.dataTransfer.getData("text/plain"), 10);
      const to = Number.parseInt(card.dataset.index, 10);
      if (Number.isFinite(from) && Number.isFinite(to)) moveTile(from, to);
    });

    const preset = el("select");
    preset.appendChild(el("option", { value: "", text: "— app preset —" }));
    PRESETS.forEach((p) => preset.appendChild(el("option", { value: p.name, text: p.name })));
    preset.onchange = (e) => {
      const p = PRESETS.find((x) => x.name === e.target.value);
      if (!p) return;
      t.id = p.name.toLowerCase().replace(/[^a-z0-9]+/g, "_");
      t.label = p.name;
      t.icon = p.icon;
      t.color = p.color;
      t.action = "app";
      t.target = { kind: "bundle", value: p.value };
      renderGrid();
    };

    const idIn = el("input", { value: t.id });
    idIn.oninput = (e) => {
      t.id = e.target.value;
    };
    const labIn = el("input", { value: t.label });
    labIn.oninput = (e) => {
      t.label = e.target.value;
    };

    const icon = el("select");
    iconOptions(t.icon).forEach((i) => {
      const o = el("option", { value: i, text: sdIcons.includes(i) ? "SD: " + i : i });
      if (i === t.icon) o.selected = true;
      icon.appendChild(o);
    });
    icon.onchange = (e) => {
      t.icon = e.target.value;
    };

    const color = el("select");
    COLORS.forEach((c) => {
      const o = el("option", { value: c, text: c });
      if (c.toLowerCase() === (t.color || "").toLowerCase()) o.selected = true;
      color.appendChild(o);
    });
    color.onchange = (e) => {
      t.color = e.target.value;
    };

    const action = el("select");
    ACTIONS.forEach((a) => {
      const o = el("option", { value: a, text: a });
      if (a === t.action) o.selected = true;
      action.appendChild(o);
    });

    const kind = el("select");
    ["bundle", "path"].forEach((k) => {
      const o = el("option", { value: k, text: k });
      if ((t.target && t.target.kind) === k) o.selected = true;
      kind.appendChild(o);
    });
    const val = el("input", {
      value: (t.target && t.target.value) || "",
      placeholder: "com.apple.Safari or /Applications/X.app",
    });

    function syncTarget() {
      if (t.action === "app") {
        t.target = { kind: kind.value, value: val.value };
        kind.disabled = false;
        val.disabled = false;
      } else {
        delete t.target;
        kind.disabled = true;
        val.disabled = true;
      }
    }
    action.onchange = (e) => {
      t.action = e.target.value;
      syncTarget();
    };
    kind.onchange = () => syncTarget();
    val.oninput = () => syncTarget();
    syncTarget();

    card.appendChild(el("label", { text: "preset" }));
    card.appendChild(preset);
    card.appendChild(el("label", { text: "id" }));
    card.appendChild(idIn);
    card.appendChild(el("label", { text: "label" }));
    card.appendChild(labIn);
    card.appendChild(el("label", { text: "icon" }));
    card.appendChild(icon);
    card.appendChild(el("label", { text: "color" }));
    card.appendChild(color);
    card.appendChild(el("label", { text: "action" }));
    card.appendChild(action);
    card.appendChild(el("label", { text: "target kind" }));
    card.appendChild(kind);
    card.appendChild(el("label", { text: "bundle / path" }));
    card.appendChild(val);
    root.appendChild(card);
  });
}

async function pngToIconBin(file, dim) {
  const image = await createImageBitmap(file);
  const canvas = document.createElement("canvas");
  canvas.width = dim;
  canvas.height = dim;
  const ctx = canvas.getContext("2d");
  if (!ctx) throw new Error("Canvas is unavailable");

  // LVGL TRUE_COLOR has no alpha; flatten transparent PNGs onto the tile background.
  ctx.fillStyle = "#161F32";
  ctx.fillRect(0, 0, dim, dim);
  const scale = Math.min(dim / image.width, dim / image.height) * 0.94;
  const width = image.width * scale;
  const height = image.height * scale;
  ctx.drawImage(image, (dim - width) / 2, (dim - height) / 2, width, height);
  image.close();

  const rgba = ctx.getImageData(0, 0, dim, dim).data;
  const output = new ArrayBuffer(8 + dim * dim * 2);
  const view = new DataView(output);
  view.setUint8(0, 0x54); // T
  view.setUint8(1, 0x44); // D
  view.setUint8(2, 0x49); // I
  view.setUint8(3, 0x31); // 1
  view.setUint16(4, dim, true);
  view.setUint16(6, dim, true);
  let offset = 8;
  for (let i = 0; i < dim * dim; i++) {
    const r = rgba[i * 4];
    const g = rgba[i * 4 + 1];
    const b = rgba[i * 4 + 2];
    const rgb565 = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
    view.setUint16(offset, rgb565, true);
    offset += 2;
  }
  return output;
}

async function uploadIcon() {
  const msg = document.getElementById("icon-msg");
  const file = document.getElementById("icon-file").files[0];
  let id = document
    .getElementById("icon-id")
    .value.trim()
    .toLowerCase()
    .replace(/[^a-z0-9_-]/g, "")
    .slice(0, 15);
  if (!file) {
    setMsg(msg, "Choose a PNG file first", false);
    return;
  }
  if (!id) {
    id = (file.name.split(".")[0] || "icon")
      .toLowerCase()
      .replace(/[^a-z0-9_-]/g, "")
      .slice(0, 15);
  }
  if (!id) {
    setMsg(msg, "Icon ID must contain letters, numbers, _ or -", false);
    return;
  }

  const dim = Number.parseInt(document.getElementById("icon-size").value, 10) || 96;
  const button = document.getElementById("icon-upload");
  button.disabled = true;
  setMsg(msg, `Converting ${file.name} to RGB565 ${dim}×${dim}…`, true);
  try {
    const data = await pngToIconBin(file, dim);
    const response = await window.touchdeck.uploadBoardIcon({
      host: boardHost(),
      id,
      data,
    });
    let json = {};
    if (response.text) {
      try {
        json = JSON.parse(response.text);
      } catch {
        throw new Error(`Invalid board response (HTTP ${response.status})`);
      }
    }
    if (!response.ok) {
      throw new Error(json.error || response.error || `HTTP ${response.status}`);
    }
    sdIcons = json.icons || sdIcons;
    renderGrid();
    setMsg(msg, `Uploaded '${id}' (${dim}px). Select it on a tile, then Save grid.`, true);
    log(`Icon '${id}' uploaded to SD`, "ok");
  } catch (e) {
    setMsg(msg, `Upload failed: ${e.message || e}`, false);
  } finally {
    button.disabled = false;
  }
}

async function loadIcons() {
  try {
    const j = await boardJson("/api/icons");
    sdIcons = j.icons || [];
  } catch {
    sdIcons = [];
  }
}

async function loadGrid() {
  const msg = document.getElementById("grid-msg");
  try {
    await loadIcons();
    grid = await boardJson("/api/grid");
    renderGrid();
    setMsg(msg, "Loaded rev " + grid.rev, true);
  } catch (e) {
    setMsg(msg, "Load failed: " + e.message, false);
  }
}

async function saveGrid() {
  const msg = document.getElementById("grid-msg");
  ensureTiles();
  grid.rev = (grid.rev || 0) + 1;
  try {
    const body = "json=" + encodeURIComponent(JSON.stringify(grid));
    const j = await boardJson("/api/grid", {
      method: "POST",
      body,
    });
    grid = j.grid || grid;
    renderGrid();
    setMsg(msg, "Saved rev " + grid.rev, true);
    log("Grid saved rev " + grid.rev, "ok");
  } catch (e) {
    setMsg(msg, String(e.message || e), false);
  }
}

async function resetGrid() {
  const msg = document.getElementById("grid-msg");
  try {
    const j = await boardJson("/api/grid/reset", { method: "POST" });
    grid = j.grid;
    renderGrid();
    setMsg(msg, "Reset to defaults", true);
  } catch (e) {
    setMsg(msg, String(e.message || e), false);
  }
}

// ---- Device settings ----

async function loadDevice() {
  const msg = document.getElementById("device-msg");
  try {
    const s = await boardJson("/api/settings");
    document.getElementById("device_name").value = s.device_name || "";
    document.getElementById("ble_name").value = s.ble_name || "";
    document.getElementById("hostname").value = s.hostname || "";
    document.getElementById("wifi_ssid").value = s.wifi_ssid || "";
    document.getElementById("wifi_password").value = "";
    document.getElementById("wifi_password").placeholder = s.wifi_password_set
      ? "set — leave blank to keep"
      : "not set";
    document.getElementById("ota_password").value = "";
    document.getElementById("ota_password").placeholder = s.ota_password_set
      ? "set — leave blank to keep"
      : "not set";
    document.getElementById("ble_enabled").checked = !!s.ble_enabled;
    document.getElementById("ble_pair_mode").checked = !!s.ble_pair_mode;
    document.getElementById("idle_dim_s").value = s.idle_dim_s ?? 30;
    document.getElementById("idle_clock_s").value = s.idle_clock_s ?? 120;
    document.getElementById("idle_dim2_s").value = s.idle_dim2_s ?? 300;
    document.getElementById("idle_off_s").value = s.idle_off_s ?? 1800;
    document.getElementById("idle_dim_pct").value = s.idle_dim_pct ?? 30;
    document.getElementById("idle_dim2_pct").value = s.idle_dim2_pct ?? 30;
    document.getElementById("clock_font_px").value = String(s.clock_font_px ?? 96);
    setMsg(msg, "Loaded device settings", true);
  } catch (e) {
    setMsg(msg, "Load failed: " + e.message, false);
  }
}

async function saveDevice() {
  const msg = document.getElementById("device-msg");
  const body = new URLSearchParams();
  body.set("device_name", document.getElementById("device_name").value.trim());
  body.set("ble_name", document.getElementById("ble_name").value.trim());
  body.set("hostname", document.getElementById("hostname").value.trim());
  body.set("wifi_ssid", document.getElementById("wifi_ssid").value.trim());
  const wifiPass = document.getElementById("wifi_password").value;
  if (wifiPass) body.set("wifi_password", wifiPass);
  const otaPass = document.getElementById("ota_password").value;
  if (otaPass) body.set("ota_password", otaPass);
  body.set("ble_enabled", document.getElementById("ble_enabled").checked ? "1" : "0");
  body.set("ble_pair_mode", document.getElementById("ble_pair_mode").checked ? "1" : "0");
  body.set("idle_dim_s", document.getElementById("idle_dim_s").value || "30");
  body.set("idle_clock_s", document.getElementById("idle_clock_s").value || "120");
  body.set("idle_dim2_s", document.getElementById("idle_dim2_s").value || "300");
  body.set("idle_off_s", document.getElementById("idle_off_s").value || "1800");
  body.set("idle_dim_pct", document.getElementById("idle_dim_pct").value || "30");
  body.set("idle_dim2_pct", document.getElementById("idle_dim2_pct").value || "30");
  body.set("clock_font_px", document.getElementById("clock_font_px").value || "96");

  try {
    setMsg(msg, "Saving… board will restart", true);
    const j = await boardJson("/api/settings", {
      method: "POST",
      body: body.toString(),
    });
    setMsg(msg, "Saved — waiting for board restart…", true);
    log("Device settings saved — board restarting", "ok");
    // Keep wanting connection; reconnect loop will recover after reboot.
    wantConnection = true;
  } catch (e) {
    // Board may close the connection mid-response while restarting.
    if (String(e.message || e).includes("Failed to fetch")) {
      setMsg(msg, "Saved — board restarting", true);
      wantConnection = true;
      return;
    }
    setMsg(msg, String(e.message || e), false);
  }
}

// ---- Tabs ----

document.querySelectorAll(".tab").forEach((btn) => {
  btn.addEventListener("click", () => {
    document.querySelectorAll(".tab").forEach((b) => b.classList.remove("active"));
    document.querySelectorAll(".panel").forEach((p) => p.classList.remove("active"));
    btn.classList.add("active");
    document.getElementById("panel-" + btn.dataset.tab).classList.add("active");
    if (btn.dataset.tab === "grid") loadGrid();
    if (btn.dataset.tab === "device") loadDevice();
  });
});

scanBtn.addEventListener("click", () => scanDevices({ autoConnect: false }));
devicesSelect.addEventListener("change", () => {
  const idx = Number.parseInt(devicesSelect.value, 10);
  if (Number.isFinite(idx)) applyDevice(idx);
});
connectBtn.addEventListener("click", connect);
disconnectBtn.addEventListener("click", disconnect);

document.getElementById("cols").onchange = (e) => {
  grid.cols = +e.target.value;
  renderGrid();
};
document.getElementById("rows").onchange = (e) => {
  grid.rows = +e.target.value;
  renderGrid();
};
document.getElementById("grid-load").onclick = loadGrid;
document.getElementById("grid-save").onclick = saveGrid;
document.getElementById("grid-reset").onclick = resetGrid;
document.getElementById("icon-upload").onclick = uploadIcon;
document.getElementById("device-load").onclick = loadDevice;
document.getElementById("device-save").onclick = saveDevice;

(async () => {
  const defaults = await window.touchdeck.getDefaults();
  hostInput.value = localStorage.getItem("host") || defaults.host;
  portInput.value = localStorage.getItem("port") || String(defaults.port);
  hostInput.addEventListener("change", () => localStorage.setItem("host", hostInput.value.trim()));
  portInput.addEventListener("change", () => localStorage.setItem("port", portInput.value.trim()));

  const devices = await scanDevices({ autoConnect: true });
  if (!devices.length) {
    log("Falling back to saved host — press Scan when the board is online.");
    connect();
  }
})();
