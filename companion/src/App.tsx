import { useCallback, useEffect, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { getBoardHost, setBoardHost } from "./boardApi";
import DevicePanel from "./DevicePanel";
import GridPanel from "./GridPanel";
import "./App.css";

type Tab = "connect" | "grid" | "device" | "profile" | "log";

type Device = { id: string; name: string; rssi?: number | null };

type ActionRecord = {
  actionId: string;
  kind: "open_app" | "open_url" | "media" | "volume" | "mute" | "keyboard";
  value: string;
  label: string;
};

type LogLine = { t: string; level: string; message: string };

type PermissionStatus = {
  accessibility: boolean;
  bluetoothReady: boolean;
  note: string;
  binaryPath: string;
};

export default function App() {
  const [tab, setTab] = useState<Tab>("connect");
  const [devices, setDevices] = useState<Device[]>([]);
  const [scanning, setScanning] = useState(false);
  const [connected, setConnected] = useState(false);
  const [connectedId, setConnectedId] = useState<string | null>(null);
  const [connectedName, setConnectedName] = useState<string | null>(null);
  const [reconnecting, setReconnecting] = useState(false);
  const [autoReconnect, setAutoReconnect] = useState(true);
  const [lastId, setLastId] = useState<string | null>(null);
  const [lastName, setLastName] = useState<string | null>(null);
  const [actions, setActions] = useState<ActionRecord[]>([]);
  const [logs, setLogs] = useState<LogLine[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [perms, setPerms] = useState<PermissionStatus | null>(null);
  const [draft, setDraft] = useState<ActionRecord>({
    actionId: "",
    kind: "open_app",
    value: "",
    label: "",
  });
  const [vkText, setVkText] = useState("");
  const [boardHost, setBoardHostState] = useState("touchdeck.local");
  const [hostDraft, setHostDraft] = useState("touchdeck.local");

  const pushLocalLog = useCallback((level: string, message: string) => {
    const t = new Date().toLocaleTimeString("en-GB", { hour12: false });
    setLogs((prev) => [...prev.slice(-200), { t, level, message }]);
  }, []);

  const refreshPerms = useCallback(async () => {
    try {
      const s = await invoke<PermissionStatus>("permissions_status");
      setPerms(s);
    } catch {
      /* ignore */
    }
  }, []);

  const refreshActions = useCallback(async () => {
    try {
      const list = await invoke<ActionRecord[]>("list_actions");
      setActions(list);
    } catch (e) {
      setError(String(e));
    }
  }, []);

  useEffect(() => {
    refreshActions();
    refreshPerms();
    getBoardHost()
      .then((h) => {
        setBoardHostState(h);
        setHostDraft(h);
      })
      .catch(() => undefined);
    const timer = setInterval(() => {
      refreshPerms();
    }, 2500);
    invoke<{
      connected: boolean;
      id?: string | null;
      name?: string | null;
      lastId?: string | null;
      lastName?: string | null;
      autoReconnect?: boolean;
      reconnecting?: boolean;
    }>("ble_status")
      .then((s) => {
        setConnected(!!s.connected);
        setConnectedId(s.id ?? null);
        setConnectedName(s.name ?? s.lastName ?? null);
        setLastId(s.lastId ?? s.id ?? null);
        setLastName(s.lastName ?? s.name ?? null);
        setAutoReconnect(s.autoReconnect !== false);
        setReconnecting(!!s.reconnecting);
        const seedId = s.id ?? s.lastId ?? null;
        const seedName = s.name ?? s.lastName ?? "TouchDeck";
        if (seedId) {
          setDevices((prev) =>
            prev.some((d) => d.id === seedId)
              ? prev.map((d) =>
                  d.id === seedId && seedName ? { ...d, name: seedName } : d,
                )
              : [{ id: seedId, name: seedName || "TouchDeck" }, ...prev],
          );
        }
      })
      .catch(() => undefined);

    const unsubs: Array<() => void> = [];
    listen<LogLine>("companion-log", (ev) => {
      setLogs((prev) => [...prev.slice(-200), ev.payload]);
    }).then((u) => unsubs.push(u));
    listen<{
      connected: boolean;
      id?: string | null;
      name?: string | null;
      lastId?: string | null;
      lastName?: string | null;
      autoReconnect?: boolean;
      reconnecting?: boolean;
    }>("ble-status", (ev) => {
      const p = ev.payload;
      setConnected(!!p.connected);
      setConnectedId(p.id ?? null);
      if (p.name !== undefined) setConnectedName(p.name ?? null);
      else if (p.lastName !== undefined) setConnectedName(p.lastName ?? null);
      if (p.lastId !== undefined) setLastId(p.lastId ?? null);
      if (p.lastName !== undefined) setLastName(p.lastName ?? null);
      if (p.autoReconnect !== undefined) setAutoReconnect(!!p.autoReconnect);
      setReconnecting(!!p.reconnecting);
      const seedId = p.connected ? p.id ?? null : p.lastId ?? p.id ?? null;
      const seedName = p.name ?? p.lastName ?? "TouchDeck";
      if (seedId && (p.connected || p.reconnecting)) {
        setDevices((prev) => {
          if (prev.some((d) => d.id === seedId)) {
            return prev.map((d) =>
              d.id === seedId && seedName ? { ...d, name: seedName } : d,
            );
          }
          return [{ id: seedId, name: seedName || "TouchDeck" }, ...prev];
        });
      }
    }).then((u) => unsubs.push(u));
    return () => {
      clearInterval(timer);
      unsubs.forEach((u) => u());
    };
  }, [refreshActions, refreshPerms]);

  async function onScan() {
    setScanning(true);
    setError(null);
    try {
      const list = await invoke<Device[]>("ble_scan");
      setDevices(list);
      await refreshPerms();
    } catch (e) {
      setError(String(e));
      await refreshPerms();
    } finally {
      setScanning(false);
    }
  }

  async function onConnect(id: string) {
    setError(null);
    try {
      const known = devices.find((d) => d.id === id);
      await invoke("ble_connect", { id });
      setConnected(true);
      setConnectedId(id);
      if (known?.name) {
        setConnectedName(known.name);
        setLastName(known.name);
      }
    } catch (e) {
      setError(String(e));
    }
  }

  async function onDisconnect() {
    try {
      await invoke("ble_disconnect");
      setConnected(false);
      setConnectedId(null);
    } catch (e) {
      setError(String(e));
    }
  }

  async function onRequestAccessibility() {
    setError(null);
    try {
      await invoke("request_accessibility");
      await refreshPerms();
    } catch (e) {
      setError(String(e));
    }
  }

  async function onRequestBluetooth() {
    setError(null);
    try {
      await invoke("request_bluetooth");
      setScanning(true);
      try {
        const list = await invoke<Device[]>("ble_scan");
        setDevices(list);
      } catch (e) {
        setError(String(e));
      } finally {
        setScanning(false);
      }
      await refreshPerms();
    } catch (e) {
      setError(String(e));
    }
  }

  async function saveAction() {
    if (!draft.actionId.trim()) {
      setError("action_id required");
      return;
    }
    setError(null);
    try {
      await invoke("upsert_action", { action: draft });
      pushLocalLog("info", `Saved ${draft.actionId}`);
      setDraft({ actionId: "", kind: "open_app", value: "", label: "" });
      await refreshActions();
    } catch (e) {
      setError(String(e));
    }
  }

  async function removeAction(actionId: string) {
    try {
      await invoke("delete_action", { actionId });
      await refreshActions();
    } catch (e) {
      setError(String(e));
    }
  }

  async function vk(kind: string, value: string) {
    if (!value.trim()) return;
    setError(null);
    try {
      await invoke("vk_inject", { kind, value });
    } catch (e) {
      setError(String(e));
    }
  }

  async function onToggleAutoReconnect() {
    const next = !autoReconnect;
    try {
      await invoke("set_auto_reconnect", { enabled: next });
      setAutoReconnect(next);
    } catch (e) {
      setError(String(e));
    }
  }

  async function saveHost() {
    const h = hostDraft.trim();
    if (!h) {
      setError("Board host required");
      return;
    }
    try {
      await setBoardHost(h);
      setBoardHostState(h);
      pushLocalLog("info", `Board host → ${h}`);
    } catch (e) {
      setError(String(e));
    }
  }

  const axOk = perms?.accessibility === true;
  const btOk = perms?.bluetoothReady === true || connected;
  const showPerms = !axOk || !btOk;

  return (
    <div className="app">
      <header className="header">
        <div>
          <div className="brand">TouchDeck</div>
          <div className="sub">Companion v0.3 · GATT</div>
        </div>
        <div
          className={`pill ${connected ? "on" : reconnecting ? "wait" : "off"}`}
        >
          {connected
            ? `Linked · ${connectedId?.slice(0, 8) ?? "ok"}`
            : reconnecting
              ? `Reconnecting · ${(lastId ?? connectedId)?.slice(0, 8) ?? "…"}`
              : "Not connected"}
        </div>
      </header>

      <nav className="tabs">
        {(["connect", "grid", "device", "profile", "log"] as Tab[]).map((t) => (
          <button key={t} className={tab === t ? "active" : ""} onClick={() => setTab(t)}>
            {t}
          </button>
        ))}
      </nav>

      {(tab === "grid" || tab === "device") && (
        <div className="board-host-bar">
          <label>
            Board host
            <input
              value={hostDraft}
              onChange={(e) => setHostDraft(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === "Enter") void saveHost();
              }}
              placeholder="touchdeck.local"
            />
          </label>
          <button type="button" className="ghost" onClick={() => void saveHost()}>
            Save host
          </button>
          <span className="muted">
            Current: <span className="mono">{boardHost}</span>
          </span>
        </div>
      )}

      {error && <div className="error">{error}</div>}

      {tab === "connect" && (
        <section className="panel">
          {showPerms && (
            <div className="perms">
              <div className="perm-banner">
                Grant permissions before using the deck. Click <strong>Request access</strong> to
                show a macOS alert and open System Settings.
                {!axOk && perms?.binaryPath ? (
                  <div className="mono muted" style={{ marginTop: 6 }}>
                    Binary: {perms.binaryPath}
                  </div>
                ) : null}
              </div>
              {!axOk && (
                <div className="perm need">
                  <div>
                    <strong>Accessibility</strong>
                    <div className="muted">
                      Required for Play / Pause / Next / Prev and keyboard simulation.
                    </div>
                  </div>
                  <div className="perm-actions">
                    <button onClick={onRequestAccessibility}>Request access</button>
                    <button
                      className="ghost"
                      onClick={() => invoke("open_permission_settings", { kind: "accessibility" })}
                    >
                      Open Settings
                    </button>
                  </div>
                </div>
              )}
              {!btOk && (
                <div className="perm need">
                  <div>
                    <strong>Bluetooth</strong>
                    <div className="muted">
                      Required to Scan / Connect over GATT. Request access (alert + Settings), then
                      Scan BLE.
                    </div>
                  </div>
                  <div className="perm-actions">
                    <button onClick={onRequestBluetooth} disabled={scanning}>
                      Request access
                    </button>
                    <button
                      className="ghost"
                      onClick={() => invoke("open_permission_settings", { kind: "bluetooth" })}
                    >
                      Open Settings
                    </button>
                  </div>
                </div>
              )}
            </div>
          )}

          <div className="row">
            <button onClick={onScan} disabled={scanning}>
              {scanning ? "Scanning…" : "Scan BLE"}
            </button>
            {connected && (
              <button className="ghost" onClick={onDisconnect}>
                Disconnect
              </button>
            )}
            <label className="auto-re">
              <input
                type="checkbox"
                checked={autoReconnect}
                onChange={() => void onToggleAutoReconnect()}
              />
              Auto-reconnect
            </label>
          </div>
          {reconnecting && !connected && (
            <p className="hint">
              Looking for last device
              {lastId ? (
                <>
                  {" "}
                  <span className="mono">{lastId.slice(0, 13)}…</span>
                </>
              ) : null}
              . Keep the board powered / in range.
            </p>
          )}
          <ul className="list">
            {devices.length === 0 && (
              <li className="muted">
                {connected
                  ? `Linked${connectedName ? ` · ${connectedName}` : connectedId ? ` · ${connectedId.slice(0, 8)}` : ""}`
                  : "No devices yet — press Scan."}
              </li>
            )}
            {devices.map((d) => (
              <li key={d.id}>
                <div>
                  <strong>
                    {d.name ||
                      (connectedId === d.id ? connectedName : null) ||
                      (lastId === d.id ? lastName : null) ||
                      "TouchDeck"}
                  </strong>
                  <div className="muted mono">{d.id}</div>
                </div>
                <button disabled={connected && connectedId === d.id} onClick={() => onConnect(d.id)}>
                  {connected && connectedId === d.id ? "Connected" : "Connect"}
                </button>
              </li>
            ))}
          </ul>

          <div className="vk">
            <h2>Virtual keyboard</h2>
            <p className="hint" style={{ marginTop: 0 }}>
              Injects keys into macOS (Accessibility required). Deck tile presses use the same
              engine when mapped in Profile.
            </p>
            <div className="vk-row">
              <button className="ghost" onClick={() => vk("volume", "volume_down")}>
                Vol −
              </button>
              <button className="ghost" onClick={() => vk("volume", "mute")}>
                Mute
              </button>
              <button className="ghost" onClick={() => vk("volume", "volume_up")}>
                Vol +
              </button>
              <button className="ghost" onClick={() => vk("keyboard", "opt+shift+volume_down")}>
                ⌥⇧Vol −
              </button>
              <button className="ghost" onClick={() => vk("keyboard", "opt+shift+volume_up")}>
                ⌥⇧Vol +
              </button>
              <button className="ghost" onClick={() => vk("media", "play_pause")}>
                Play/Pause
              </button>
              <button className="ghost" onClick={() => vk("media", "previous")}>
                Prev
              </button>
              <button className="ghost" onClick={() => vk("media", "next")}>
                Next
              </button>
            </div>
            <div className="vk-row">
              <button className="ghost" onClick={() => vk("keyboard", "cmd+c")}>
                ⌘C
              </button>
              <button className="ghost" onClick={() => vk("keyboard", "cmd+v")}>
                ⌘V
              </button>
              <button className="ghost" onClick={() => vk("keyboard", "cmd+z")}>
                ⌘Z
              </button>
              <button className="ghost" onClick={() => vk("keyboard", "cmd+space")}>
                ⌘Space
              </button>
              <button className="ghost" onClick={() => vk("keyboard", "cmd+tab")}>
                ⌘Tab
              </button>
              <button className="ghost" onClick={() => vk("keyboard", "cmd+shift+p")}>
                ⌘⇧P
              </button>
            </div>
            <div className="vk-type">
              <input
                placeholder="Type text to inject…"
                value={vkText}
                onChange={(e) => setVkText(e.target.value)}
                onKeyDown={(e) => {
                  if (e.key === "Enter") void vk("keyboard", vkText);
                }}
              />
              <button
                onClick={() => vk("keyboard", vkText)}
                disabled={!vkText.trim()}
              >
                Type
              </button>
            </div>
          </div>

          <p className="hint">
            Enable pairing mode on the board. After granting access, turn on the toggle for{" "}
            <em>TouchDeck Companion</em> in System Settings → Privacy &amp; Security.
          </p>
        </section>
      )}

      {tab === "grid" && (
        <GridPanel host={boardHost} onLog={pushLocalLog} />
      )}

      {tab === "device" && (
        <DevicePanel host={boardHost} onLog={pushLocalLog} />
      )}

      {tab === "profile" && (
        <section className="panel">
          <h2>action_id map</h2>
          <div className="form">
            <input
              placeholder="action_id"
              value={draft.actionId}
              onChange={(e) => setDraft({ ...draft, actionId: e.target.value })}
            />
            <input
              placeholder="label"
              value={draft.label}
              onChange={(e) => setDraft({ ...draft, label: e.target.value })}
            />
            <select
              value={draft.kind}
              onChange={(e) =>
                setDraft({ ...draft, kind: e.target.value as ActionRecord["kind"] })
              }
            >
              <option value="open_app">open_app</option>
              <option value="open_url">open_url</option>
              <option value="media">media</option>
              <option value="volume">volume</option>
              <option value="mute">mute</option>
              <option value="keyboard">keyboard</option>
            </select>
            <input
              placeholder="bundle / path / media op / cmd+c"
              value={draft.value}
              onChange={(e) => setDraft({ ...draft, value: e.target.value })}
            />
            <button onClick={saveAction}>Save</button>
          </div>
          <p className="hint" style={{ marginTop: 0, marginBottom: 12 }}>
            Keyboard examples: <code>cmd+c</code>, <code>opt+shift+volume_up</code>,{" "}
            <code>cmd+space</code>, or plain text to type. Media fine volume:{" "}
            <code>volume_up_fine</code> / <code>volume_down_fine</code>.
          </p>
          <ul className="list">
            {actions.map((a) => (
              <li key={a.actionId}>
                <div>
                  <strong>{a.label || a.actionId}</strong>
                  <div className="muted mono">
                    {a.actionId} · {a.kind} · {a.value}
                  </div>
                </div>
                <button className="ghost" onClick={() => removeAction(a.actionId)}>
                  Delete
                </button>
              </li>
            ))}
          </ul>
        </section>
      )}

      {tab === "log" && (
        <section className="panel log">
          <pre>
            {logs.length === 0
              ? "Waiting for events…"
              : logs.map((l) => `${l.t} [${l.level}] ${l.message}`).join("\n")}
          </pre>
        </section>
      )}
    </div>
  );
}
