/**
 * TouchDeck web flasher — Web Serial + esptool-js
 * Board: JC8048W550C (ESP32-S3, 16 MB flash, QIO @ 80 MHz)
 */

import { ESPLoader, Transport } from "https://esm.sh/esptool-js@0.5.2";
import CryptoJS from "https://esm.sh/crypto-js@4.2.0";

const $ = (id) => document.getElementById(id);

const els = {
  status: $("status"),
  chip: $("chip"),
  progressWrap: $("progress-wrap"),
  progressBar: $("progress-bar"),
  progressLabel: $("progress-label"),
  log: $("log"),
  btnConnect: $("btn-connect"),
  btnFlash: $("btn-flash"),
  btnErase: $("btn-erase"),
  btnDisconnect: $("btn-disconnect"),
  firmwareVersion: $("firmware-version"),
  eraseAll: $("erase-all"),
  fileInput: $("file-firmware"),
};

let port = null;
let transport = null;
let loader = null;
let device = null;
let manifest = null;

function log(msg, level = "info") {
  const line = document.createElement("div");
  line.className = level;
  const t = new Date().toLocaleTimeString();
  line.textContent = `[${t}] ${msg}`;
  els.log.appendChild(line);
  els.log.scrollTop = els.log.scrollHeight;
}

function setStatus(text, tone = "info") {
  els.status.textContent = text;
  els.status.dataset.tone = tone;
}

function setProgress(percent, label = "") {
  els.progressWrap.hidden = percent < 0;
  if (percent < 0) return;
  els.progressBar.style.width = `${Math.min(100, Math.max(0, percent))}%`;
  els.progressLabel.textContent = label || `${percent}%`;
}

function md5Hex(image) {
  const words = [];
  for (let i = 0; i < image.length; i += 4) {
    words.push(
      ((image[i] ?? 0) << 24) |
        ((image[i + 1] ?? 0) << 16) |
        ((image[i + 2] ?? 0) << 8) |
        (image[i + 3] ?? 0),
    );
  }
  const wordArray = CryptoJS.lib.WordArray.create(words, image.length);
  return CryptoJS.MD5(wordArray).toString();
}

function webSerialSupported() {
  return typeof navigator !== "undefined" && "serial" in navigator;
}

function manifestBaseUrl() {
  if (manifest?.firmwareBaseUrl) {
    const base = manifest.firmwareBaseUrl;
    return base.endsWith("/") ? base : `${base}/`;
  }
  return new URL("./firmware/", window.location.href).href;
}

function partUrl(partPath) {
  return new URL(partPath, manifestBaseUrl()).href;
}

async function loadManifest() {
  const res = await fetch("./manifest.json", { cache: "no-store" });
  if (!res.ok) throw new Error(`Cannot load manifest (${res.status})`);
  manifest = await res.json();
  els.firmwareVersion.textContent = manifest.version || "—";
  if (manifest.firmwareBaseUrl) {
    log(`Firmware CDN: ${manifest.firmwareBaseUrl}`, "ok");
  }
  return manifest;
}

async function loadPartsFromManifest(chipName) {
  const build =
    manifest.builds.find((b) => chipName.includes(b.chipFamily.replace("-", ""))) ||
    manifest.builds.find((b) => chipName.toUpperCase().includes(b.chipFamily.toUpperCase())) ||
    manifest.builds[0];
  if (!build) throw new Error("No build entry in manifest for this chip");

  const base = manifestBaseUrl();
  const parts = [];
  for (const part of build.parts) {
    const url = partUrl(part.path);
    log(`Downloading ${part.path} from ${manifest?.firmwareBaseUrl ? "GitHub Releases" : "local"}…`);
    const res = await fetch(url, { cache: "no-store" });
    if (!res.ok) throw new Error(`Missing ${part.path} (${res.status}). Run scripts/prepare-web-firmware.sh`);
    const buf = await res.arrayBuffer();
    parts.push({ data: new Uint8Array(buf), address: part.offset, path: part.path });
  }
  return parts;
}

async function loadPartsFromFile(file) {
  const buf = await file.arrayBuffer();
  return [{ data: new Uint8Array(buf), address: 0x10000, path: file.name }];
}

function setButtons(connected) {
  els.btnConnect.disabled = connected;
  els.btnFlash.disabled = !connected;
  els.btnErase.disabled = !connected;
  els.btnDisconnect.disabled = !connected;
}

async function connectDevice() {
  if (!webSerialSupported()) {
    throw new Error("Web Serial is not available. Use Chrome or Edge on a desktop.");
  }

  setStatus("Connecting…", "warn");
  log("Select the ESP32-S3 USB serial port…");

  port = await navigator.serial.requestPort();
  transport = new Transport(port, true);
  const terminal = {
    clean() {},
    writeLine: (d) => log(d, "debug"),
    write: (d) => log(d, "debug"),
  };

  loader = new ESPLoader({ transport, baudrate: 115200, terminal });
  log("Entering bootloader…");
  const chipName = await loader.main("default_reset");

  let mac = "";
  let flashSize = "";
  try {
    mac = await loader.chip.readMac(loader);
  } catch {
    /* optional */
  }
  try {
    flashSize = await loader.detectFlashSize();
  } catch {
    /* optional */
  }

  device = { chipName, mac, flashSize };
  els.chip.textContent = [chipName, mac ? `MAC ${mac}` : "", flashSize ? `Flash ${flashSize}` : ""]
    .filter(Boolean)
    .join(" · ");

  if (!chipName.toUpperCase().includes("S3")) {
    log(`Warning: TouchDeck expects ESP32-S3, detected ${chipName}`, "err");
  }

  setStatus("Connected — ready to flash", "ok");
  log(`Connected ${chipName}`, "ok");
  setButtons(true);
}

async function disconnectDevice() {
  try {
    if (port) await port.close();
  } catch {
    /* ignore */
  }
  port = null;
  transport = null;
  loader = null;
  device = null;
  setButtons(false);
  setProgress(-1);
  setStatus("Not connected", "info");
  els.chip.textContent = "—";
  log("Disconnected");
}

async function flashDevice() {
  if (!loader || !device) throw new Error("Device not connected");

  let parts;
  const file = els.fileInput.files?.[0];
  if (file) {
    log(`Using custom file: ${file.name}`);
    parts = await loadPartsFromFile(file);
  } else {
    parts = await loadPartsFromManifest(device.chipName);
  }

  const flashSize = (await loader.detectFlashSize()) || "16MB";
  const flashMode = manifest?.flashMode || "qio";
  const flashFreq = manifest?.flashFreq || "80m";
  const eraseAll = els.eraseAll.checked;

  setStatus("Writing firmware…", "warn");
  setProgress(0, "0%");
  log(`Flash ${parts.length} part(s) · mode=${flashMode} freq=${flashFreq} size=${flashSize}`);

  await loader.writeFlash({
    fileArray: parts.map((p) => ({ data: p.data, address: p.address })),
    flashMode,
    flashFreq,
    flashSize,
    eraseAll,
    compress: true,
    reportProgress: (fileIndex, written, total) => {
      const pct = total > 0 ? Math.round((written / total) * 100) : 0;
      setProgress(pct, `Part ${fileIndex + 1}/${parts.length} · ${pct}%`);
    },
    calculateMD5Hash: md5Hex,
  });

  setProgress(100, "Done");
  setStatus("Flash complete — resetting", "ok");
  log("Flash write finished", "ok");
  await loader.after("hard_reset");
  log("Device reset. Open Serial Monitor (115200) if needed.", "ok");
  setStatus("Flash succeeded", "ok");
}

async function eraseFlash() {
  if (!loader) throw new Error("Device not connected");
  if (!confirm("Erase the entire flash? The current firmware will be removed.")) return;
  setStatus("Erasing flash…", "warn");
  log("Erase flash…");
  await loader.eraseFlash();
  setStatus("Flash erased", "ok");
  log("Erase complete", "ok");
}

async function safeRun(fn) {
  try {
    await fn();
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    setStatus("Error", "err");
    log(msg, "err");
  }
}

els.btnConnect.addEventListener("click", () => safeRun(connectDevice));
els.btnDisconnect.addEventListener("click", () => safeRun(disconnectDevice));
els.btnFlash.addEventListener("click", () => safeRun(flashDevice));
els.btnErase.addEventListener("click", () => safeRun(eraseFlash));

setButtons(false);
setProgress(-1);

if (!webSerialSupported()) {
  setStatus("Chrome or Edge required (Web Serial)", "err");
  log("This browser does not support the Web Serial API.", "err");
}

safeRun(async () => {
  await loadManifest();
  log(`Manifest TouchDeck v${manifest.version}`, "ok");
});
