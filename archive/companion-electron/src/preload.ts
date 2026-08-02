import { contextBridge, ipcRenderer } from "electron";

contextBridge.exposeInMainWorld("touchdeck", {
  launchApp: (target: { kind: "bundle" | "path"; value: string }) =>
    ipcRenderer.invoke("launch-app", target),
  getVolume: () => ipcRenderer.invoke("get-volume"),
  adjustVolume: (delta: 3 | -3) => ipcRenderer.invoke("adjust-volume", delta),
  refreshVolume: () => ipcRenderer.invoke("refresh-volume"),
  onHostVolume: (cb: (v: { level: number; muted: boolean }) => void) =>
    ipcRenderer.on("host-volume", (_e, v) => cb(v)),
  onApprovalUpdate: (cb: (payload: { pending: Array<{ id: string; source: string; title: string; body: string }> }) => void) =>
    ipcRenderer.on("approval-update", (_e, payload) => cb(payload)),
  setConnectionStatus: (name: string) => ipcRenderer.invoke("set-connection-status", name),
  getDefaults: () => ipcRenderer.invoke("get-defaults") as Promise<{ host: string; port: number }>,
  discoverDevices: () =>
    ipcRenderer.invoke("discover-devices") as Promise<{
      ok: boolean;
      devices: Array<{ name: string; host: string; port: number }>;
      error?: string;
    }>,
  boardRequest: (request: { host: string; path: string; method?: string; body?: string }) =>
    ipcRenderer.invoke("board-request", request) as Promise<{
      ok: boolean;
      status: number;
      text: string;
      error?: string;
    }>,
  uploadBoardIcon: (request: { host: string; id: string; data: ArrayBuffer }) =>
    ipcRenderer.invoke("upload-board-icon", request) as Promise<{
      ok: boolean;
      status: number;
      text: string;
      error?: string;
    }>,
  showError: (title: string, body: string) => ipcRenderer.invoke("show-error", title, body),
});
