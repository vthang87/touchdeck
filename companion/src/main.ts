import {
  app,
  BrowserWindow,
  Tray,
  Menu,
  nativeImage,
  ipcMain,
  dialog,
  shell,
} from "electron";
import { execFile, spawn } from "child_process";
import * as dns from "dns";
import * as path from "path";
import { promisify } from "util";
import { approvalKey, scanApprovalRequests, type ApprovalPending } from "./approval_watcher";
import { startWebInstallServer, stopWebInstallServer, webInstallUrl } from "./web_install_server";

const execFileAsync = promisify(execFile);
const dnsLookup = promisify(dns.lookup);

const DEFAULT_HOST = "touchdeck.local";
const DEFAULT_PORT = 81;
const VOLUME_POLL_MS = 2000;
const APPROVAL_POLL_MS = 2500;
const DISCOVER_TIMEOUT_MS = 3500;

type LaunchTarget = {
  kind: "bundle" | "path";
  value: string;
};

type DiscoveredDevice = {
  name: string;
  host: string;
  port: number;
};

let tray: Tray | null = null;
let win: BrowserWindow | null = null;
let connectedName = "Disconnected";
let volumeTimer: NodeJS.Timeout | null = null;
let approvalTimer: NodeJS.Timeout | null = null;
let lastVolumeKey = "";
let lastApprovalKey = "";
let lastApprovalError = "";

function setTrayTooltip() {
  tray?.setToolTip(`TouchDeck — ${connectedName}`);
}

function rebuildMenu() {
  if (!tray) return;
  const menu = Menu.buildFromTemplate([
    { label: `Status: ${connectedName}`, enabled: false },
    { type: "separator" },
    {
      label: "Flash ESP (browser)",
      click: () => {
        startWebInstallServer();
        void shell.openExternal(webInstallUrl());
      },
    },
    { type: "separator" },
    {
      label: "Open companion window",
      click: () => {
        win?.show();
        win?.focus();
      },
    },
    { label: "Quit", click: () => app.quit() },
  ]);
  tray.setContextMenu(menu);
}

function createTray() {
  tray = new Tray(nativeImage.createEmpty());
  tray.setTitle("TD");
  setTrayTooltip();
  rebuildMenu();
}

function createWindow() {
  win = new BrowserWindow({
    width: 720,
    height: 780,
    show: true,
    title: "TouchDeck Companion",
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false,
      // The window is hidden most of the time; keep its socket loop responsive.
      backgroundThrottling: false,
    },
  });

  win.loadFile(path.join(__dirname, "renderer", "index.html"));
  win.on("close", (e) => {
    e.preventDefault();
    win?.hide();
  });
}

function isSafeTarget(target: LaunchTarget): boolean {
  if (!target || !target.value || target.value.length > 200) return false;
  if (/[;&|`$<>\n\r]/.test(target.value)) return false;
  if (target.kind === "bundle") {
    return /^[A-Za-z0-9.-]+$/.test(target.value);
  }
  if (target.kind === "path") {
    return target.value.startsWith("/") && target.value.endsWith(".app");
  }
  return false;
}

type HostVolume = { ok: boolean; level?: number; muted?: boolean; error?: string };

// macOS keeps the pre-mute level in `output volume`, so mute is reported separately
// rather than inferred from a zero level.
async function getHostVolume(): Promise<HostVolume> {
  try {
    const { stdout } = await execFileAsync("/usr/bin/osascript", [
      "-e",
      "set s to (get volume settings)",
      "-e",
      '(output volume of s as text) & "," & (output muted of s as text)',
    ]);
    const [rawLevel, rawMuted] = stdout.trim().split(",");
    const level = Number.parseInt(rawLevel, 10);
    if (!Number.isFinite(level) || level < 0 || level > 100) {
      return { ok: false, error: `Unexpected volume output: ${stdout.trim()}` };
    }
    return { ok: true, level, muted: rawMuted?.trim() === "true" };
  } catch (err) {
    return { ok: false, error: err instanceof Error ? err.message : String(err) };
  }
}

async function adjustHostVolume(delta: number): Promise<HostVolume> {
  if (delta !== 3 && delta !== -3) {
    return { ok: false, error: "Volume delta must be +3 or -3" };
  }
  try {
    await execFileAsync("/usr/bin/osascript", [
      "-e",
      "set currentVolume to output volume of (get volume settings)",
      "-e",
      `set targetVolume to currentVolume + (${delta})`,
      "-e",
      "if targetVolume < 0 then set targetVolume to 0",
      "-e",
      "if targetVolume > 100 then set targetVolume to 100",
      "-e",
      "set volume output volume targetVolume",
    ]);
    return getHostVolume();
  } catch (err) {
    return { ok: false, error: err instanceof Error ? err.message : String(err) };
  }
}

function sendApprovalToRenderer(pending: ApprovalPending[], force = false) {
  if (!win || win.isDestroyed()) return;
  const key = approvalKey(pending);
  if (!force && key === lastApprovalKey) return;
  lastApprovalKey = key;
  win.webContents.send("approval-update", { pending });
}

function startApprovalWatch() {
  if (approvalTimer) return;
  approvalTimer = setInterval(async () => {
    const result = await scanApprovalRequests();
    if (!result.ok) {
      if (result.error && result.error !== lastApprovalError) {
        lastApprovalError = result.error;
        console.warn(`[approval] scan failed: ${result.error}`);
      }
      return;
    }
    lastApprovalError = "";
    sendApprovalToRenderer(result.pending);
  }, APPROVAL_POLL_MS);
}

function stopApprovalWatch() {
  if (approvalTimer) {
    clearInterval(approvalTimer);
    approvalTimer = null;
  }
  lastApprovalKey = "";
  lastApprovalError = "";
  if (win && !win.isDestroyed()) {
    win.webContents.send("approval-update", { pending: [] });
  }
}

function sendVolumeToRenderer(v: HostVolume, force: boolean) {
  if (!v.ok || !win || win.isDestroyed()) return;
  const key = `${v.level}/${v.muted}`;
  if (!force && key === lastVolumeKey) return;
  lastVolumeKey = key;
  win.webContents.send("host-volume", { level: v.level, muted: v.muted });
}

// Polling lives in the main process on purpose: Chromium throttles renderer
// timers while the companion window is hidden, which stalled the sync.
function startVolumeWatch() {
  if (volumeTimer) return;
  volumeTimer = setInterval(async () => {
    sendVolumeToRenderer(await getHostVolume(), false);
  }, VOLUME_POLL_MS);
}

function stopVolumeWatch() {
  if (volumeTimer) {
    clearInterval(volumeTimer);
    volumeTimer = null;
  }
  lastVolumeKey = "";
}

async function launchApp(target: LaunchTarget): Promise<{ ok: boolean; error?: string }> {
  if (!isSafeTarget(target)) {
    return { ok: false, error: "Rejected unsafe launch target" };
  }
  try {
    const args = target.kind === "bundle" ? ["-b", target.value] : [target.value];
    await execFileAsync("/usr/bin/open", args);
    return { ok: true };
  } catch (err) {
    return { ok: false, error: err instanceof Error ? err.message : String(err) };
  }
}

function browseMdnsInstances(timeoutMs: number): Promise<string[]> {
  return new Promise((resolve) => {
    const child = spawn("/usr/bin/dns-sd", ["-B", "_touchdeck._tcp", "local."], {
      stdio: ["ignore", "pipe", "ignore"],
    });
    const names = new Set<string>();
    let settled = false;

    const finish = () => {
      if (settled) return;
      settled = true;
      try {
        child.kill("SIGTERM");
      } catch {
        // ignore
      }
      resolve([...names]);
    };

    const timer = setTimeout(finish, timeoutMs);

    child.stdout.setEncoding("utf8");
    child.stdout.on("data", (chunk: string) => {
      for (const line of chunk.split("\n")) {
        // Example: ... Add  3  11 local.  _touchdeck._tcp.  touchdeck
        if (!/\sAdd\s/.test(line) || !line.includes("_touchdeck._tcp")) continue;
        const parts = line.trim().split(/\s+/);
        const name = parts[parts.length - 1];
        if (name && name !== "Name" && name !== "Instance") {
          names.add(name);
        }
      }
    });

    child.on("error", () => {
      clearTimeout(timer);
      finish();
    });
    child.on("exit", () => {
      clearTimeout(timer);
      finish();
    });
  });
}

function resolveMdnsInstance(instance: string, timeoutMs = 2500): Promise<DiscoveredDevice | null> {
  return new Promise((resolve) => {
    const child = spawn("/usr/bin/dns-sd", ["-L", instance, "_touchdeck._tcp", "local."], {
      stdio: ["ignore", "pipe", "ignore"],
    });
    let settled = false;
    let buf = "";

    const finish = (device: DiscoveredDevice | null) => {
      if (settled) return;
      settled = true;
      try {
        child.kill("SIGTERM");
      } catch {
        // ignore
      }
      resolve(device);
    };

    const timer = setTimeout(() => finish(null), timeoutMs);

    child.stdout.setEncoding("utf8");
    child.stdout.on("data", (chunk: string) => {
      buf += chunk;
      // "... can be reached at touchdeck.local.:81 (interface 11)"
      const m = buf.match(/can be reached at\s+([^\s]+?):(\d+)/i);
      if (!m) return;
      const host = m[1].replace(/\.$/, "");
      const port = Number.parseInt(m[2], 10);
      if (!host || !Number.isFinite(port)) return;
      clearTimeout(timer);
      finish({ name: instance, host, port });
    });

    child.on("error", () => {
      clearTimeout(timer);
      finish(null);
    });
    child.on("exit", () => {
      clearTimeout(timer);
      if (!settled) finish(null);
    });
  });
}

async function fallbackLocalLookup(): Promise<DiscoveredDevice[]> {
  const candidates = [DEFAULT_HOST, "touchdeck.local"];
  const found: DiscoveredDevice[] = [];
  for (const host of candidates) {
    try {
      await dnsLookup(host);
      found.push({ name: host.replace(/\.local$/, ""), host, port: DEFAULT_PORT });
    } catch {
      // not reachable via system resolver
    }
  }
  return found;
}

async function discoverDevices(): Promise<{ ok: boolean; devices: DiscoveredDevice[]; error?: string }> {
  try {
    const instances = await browseMdnsInstances(DISCOVER_TIMEOUT_MS);
    const devices: DiscoveredDevice[] = [];
    for (const name of instances) {
      const resolved = await resolveMdnsInstance(name);
      if (resolved) devices.push(resolved);
    }

    if (devices.length === 0) {
      devices.push(...(await fallbackLocalLookup()));
    }

    // De-dupe by host:port
    const uniq = new Map<string, DiscoveredDevice>();
    for (const d of devices) {
      uniq.set(`${d.host}:${d.port}`, d);
    }
    return { ok: true, devices: [...uniq.values()] };
  } catch (err) {
    return {
      ok: false,
      devices: [],
      error: err instanceof Error ? err.message : String(err),
    };
  }
}

type BoardResponse = {
  ok: boolean;
  status: number;
  text: string;
  error?: string;
};

const BOARD_API_PATHS = new Set([
  "/api/grid",
  "/api/grid/reset",
  "/api/icons",
  "/api/settings",
]);

function validBoardHost(host: string): boolean {
  return typeof host === "string" && host.length <= 253 && /^[A-Za-z0-9.-]+$/.test(host);
}

async function boardRequest(
  host: string,
  apiPath: string,
  method = "GET",
  body = "",
): Promise<BoardResponse> {
  if (!validBoardHost(host) || !BOARD_API_PATHS.has(apiPath)) {
    return { ok: false, status: 400, text: "", error: "Invalid board request" };
  }
  if (method !== "GET" && method !== "POST") {
    return { ok: false, status: 405, text: "", error: "Unsupported method" };
  }
  try {
    const response = await fetch(`http://${host}${apiPath}`, {
      method,
      headers: body ? { "Content-Type": "application/x-www-form-urlencoded" } : undefined,
      body: body || undefined,
      signal: AbortSignal.timeout(12000),
    });
    const text = await response.text();
    return { ok: response.ok, status: response.status, text };
  } catch (err) {
    return {
      ok: false,
      status: 0,
      text: "",
      error: err instanceof Error ? err.message : String(err),
    };
  }
}

async function uploadBoardIcon(
  host: string,
  id: string,
  data: ArrayBuffer,
): Promise<BoardResponse> {
  if (!validBoardHost(host) || !/^[a-z0-9_-]{1,15}$/.test(id)) {
    return { ok: false, status: 400, text: "", error: "Invalid host or icon id" };
  }
  if (!(data instanceof ArrayBuffer) || data.byteLength < 10 || data.byteLength > 32776) {
    return { ok: false, status: 400, text: "", error: "Invalid icon data" };
  }
  try {
    const form = new FormData();
    const copy = data.slice(0);
    form.append("file", new Blob([copy]), `${id}.bin`);
    const response = await fetch(`http://${host}/api/icon`, {
      method: "POST",
      body: form,
      signal: AbortSignal.timeout(20000),
    });
    const text = await response.text();
    return { ok: response.ok, status: response.status, text };
  } catch (err) {
    return {
      ok: false,
      status: 0,
      text: "",
      error: err instanceof Error ? err.message : String(err),
    };
  }
}

app.whenReady().then(() => {
  createTray();
  createWindow();
  startApprovalWatch();
  startWebInstallServer();
});

ipcMain.handle("launch-app", async (_e, target: LaunchTarget) => launchApp(target));

ipcMain.handle("get-volume", async () => getHostVolume());

ipcMain.handle("adjust-volume", async (_e, delta: number) => {
  const volume = await adjustHostVolume(delta);
  sendVolumeToRenderer(volume, true);
  return volume;
});

ipcMain.handle("refresh-volume", async () => {
  sendVolumeToRenderer(await getHostVolume(), true);
  return true;
});

ipcMain.handle("set-connection-status", async (_e, name: string) => {
  connectedName = name || "Disconnected";
  setTrayTooltip();
  rebuildMenu();
  if (connectedName === "Disconnected") {
    stopVolumeWatch();
    stopApprovalWatch();
  } else {
    startVolumeWatch();
    startApprovalWatch();
    sendVolumeToRenderer(await getHostVolume(), true);
    const approval = await scanApprovalRequests();
    if (approval.ok) {
      sendApprovalToRenderer(approval.pending, true);
    }
  }
  return true;
});

ipcMain.handle("get-defaults", async () => ({ host: DEFAULT_HOST, port: DEFAULT_PORT }));

ipcMain.handle("discover-devices", async () => discoverDevices());

ipcMain.handle(
  "board-request",
  async (_e, request: { host: string; path: string; method?: string; body?: string }) =>
    boardRequest(request.host, request.path, request.method, request.body),
);

ipcMain.handle(
  "upload-board-icon",
  async (_e, request: { host: string; id: string; data: ArrayBuffer }) =>
    uploadBoardIcon(request.host, request.id, request.data),
);

ipcMain.handle("show-error", async (_e, title: string, body: string) => {
  if (win) {
    await dialog.showMessageBox(win, { type: "error", title, message: body });
  }
});

app.on("window-all-closed", () => {
  // Keep the menu-bar process alive.
});

app.on("before-quit", () => {
  stopApprovalWatch();
  stopVolumeWatch();
  stopWebInstallServer();
  if (win) {
    win.removeAllListeners("close");
    win.close();
  }
});
