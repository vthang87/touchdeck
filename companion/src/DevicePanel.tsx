import { useCallback, useEffect, useState } from "react";
import { boardRequest } from "./boardApi";

type DeviceSettings = {
  device_name: string;
  ble_name: string;
  hostname: string;
  wifi_ssid: string;
  wifi_password: string;
  ota_password: string;
  wifi_password_set: boolean;
  ota_password_set: boolean;
  ble_enabled: boolean;
  ble_pair_mode: boolean;
  idle_dim_s: number;
  idle_clock_s: number;
  idle_dim2_s: number;
  idle_off_s: number;
  idle_dim_pct: number;
  idle_dim2_pct: number;
  clock_font_px: number;
};

const EMPTY: DeviceSettings = {
  device_name: "",
  ble_name: "",
  hostname: "",
  wifi_ssid: "",
  wifi_password: "",
  ota_password: "",
  wifi_password_set: false,
  ota_password_set: false,
  ble_enabled: true,
  ble_pair_mode: true,
  idle_dim_s: 30,
  idle_clock_s: 120,
  idle_dim2_s: 300,
  idle_off_s: 1800,
  idle_dim_pct: 30,
  idle_dim2_pct: 30,
  clock_font_px: 96,
};

type Props = {
  host: string;
  onLog?: (level: string, message: string) => void;
};

export default function DevicePanel({ host, onLog }: Props) {
  const [s, setS] = useState<DeviceSettings>(EMPTY);
  const [msg, setMsg] = useState<{ text: string; ok: boolean } | null>(null);
  const [busy, setBusy] = useState(false);

  const load = useCallback(async () => {
    setBusy(true);
    setMsg(null);
    try {
      const raw = (await boardRequest("/api/settings", { host })) as Record<string, unknown>;
      setS({
        device_name: String(raw.device_name ?? ""),
        ble_name: String(raw.ble_name ?? ""),
        hostname: String(raw.hostname ?? ""),
        wifi_ssid: String(raw.wifi_ssid ?? ""),
        wifi_password: "",
        ota_password: "",
        wifi_password_set: !!raw.wifi_password_set,
        ota_password_set: !!raw.ota_password_set,
        ble_enabled: !!raw.ble_enabled,
        ble_pair_mode: !!raw.ble_pair_mode,
        idle_dim_s: Number(raw.idle_dim_s ?? 30),
        idle_clock_s: Number(raw.idle_clock_s ?? 120),
        idle_dim2_s: Number(raw.idle_dim2_s ?? 300),
        idle_off_s: Number(raw.idle_off_s ?? 1800),
        idle_dim_pct: Number(raw.idle_dim_pct ?? 30),
        idle_dim2_pct: Number(raw.idle_dim2_pct ?? 30),
        clock_font_px: Number(raw.clock_font_px ?? 96),
      });
      setMsg({ text: "Loaded device settings", ok: true });
      onLog?.("info", "Device settings loaded");
    } catch (e) {
      setMsg({ text: String(e), ok: false });
      onLog?.("error", `Device load failed: ${e}`);
    } finally {
      setBusy(false);
    }
  }, [host, onLog]);

  useEffect(() => {
    void load();
  }, [load]);

  async function save() {
    setBusy(true);
    setMsg({ text: "Saving… board will restart", ok: true });
    const body = new URLSearchParams();
    body.set("device_name", s.device_name.trim());
    body.set("ble_name", s.ble_name.trim());
    body.set("hostname", s.hostname.trim());
    body.set("wifi_ssid", s.wifi_ssid.trim());
    if (s.wifi_password) body.set("wifi_password", s.wifi_password);
    if (s.ota_password) body.set("ota_password", s.ota_password);
    body.set("ble_enabled", s.ble_enabled ? "1" : "0");
    body.set("ble_pair_mode", s.ble_pair_mode ? "1" : "0");
    body.set("idle_dim_s", String(s.idle_dim_s || 30));
    body.set("idle_clock_s", String(s.idle_clock_s || 120));
    body.set("idle_dim2_s", String(s.idle_dim2_s || 300));
    body.set("idle_off_s", String(s.idle_off_s || 1800));
    body.set("idle_dim_pct", String(s.idle_dim_pct || 30));
    body.set("idle_dim2_pct", String(s.idle_dim2_pct || 30));
    body.set("clock_font_px", String(s.clock_font_px || 96));

    try {
      await boardRequest("/api/settings", {
        host,
        method: "POST",
        body: body.toString(),
      });
      setMsg({ text: "Saved — board restarting…", ok: true });
      onLog?.("info", "Device settings saved — board restarting");
    } catch (e) {
      const err = String(e);
      // Board often drops the connection mid-response while restarting.
      if (/Failed to fetch|error sending|Connection reset|timed out|connection/i.test(err)) {
        setMsg({ text: "Saved — board restarting", ok: true });
        onLog?.("info", "Device settings saved — board restarting");
      } else {
        setMsg({ text: err, ok: false });
        onLog?.("error", `Device save failed: ${err}`);
      }
    } finally {
      setBusy(false);
    }
  }

  function field<K extends keyof DeviceSettings>(key: K, value: DeviceSettings[K]) {
    setS((prev) => ({ ...prev, [key]: value }));
  }

  return (
    <section className="panel">
      <h2>Device</h2>
      <p className="hint" style={{ marginTop: 0 }}>
        Configure the board over Wi‑Fi (same as the old companion Device tab). Board must be on the
        network — use host above (e.g. <code>touchdeck.local</code> or IP).
      </p>

      <div className="form device-form">
        <label>
          Device name
          <input value={s.device_name} onChange={(e) => field("device_name", e.target.value)} />
        </label>
        <label>
          Bluetooth name
          <input value={s.ble_name} onChange={(e) => field("ble_name", e.target.value)} />
        </label>
        <label>
          Hostname
          <input value={s.hostname} onChange={(e) => field("hostname", e.target.value)} />
        </label>
        <label>
          Wi‑Fi SSID
          <input value={s.wifi_ssid} onChange={(e) => field("wifi_ssid", e.target.value)} />
        </label>
        <label>
          Wi‑Fi password
          <input
            type="password"
            value={s.wifi_password}
            placeholder={s.wifi_password_set ? "set — leave blank to keep" : "not set"}
            onChange={(e) => field("wifi_password", e.target.value)}
          />
        </label>
        <label>
          OTA password
          <input
            type="password"
            value={s.ota_password}
            placeholder={s.ota_password_set ? "set — leave blank to keep" : "not set"}
            onChange={(e) => field("ota_password", e.target.value)}
          />
        </label>

        <label className="check">
          <input
            type="checkbox"
            checked={s.ble_enabled}
            onChange={(e) => field("ble_enabled", e.target.checked)}
          />
          Bluetooth enabled
        </label>
        <label className="check">
          <input
            type="checkbox"
            checked={s.ble_pair_mode}
            onChange={(e) => field("ble_pair_mode", e.target.checked)}
          />
          Pairing mode (discoverable)
        </label>

        <div className="device-grid">
          <label>
            Idle dim (s)
            <input
              type="number"
              min={0}
              max={65535}
              value={s.idle_dim_s}
              onChange={(e) => field("idle_dim_s", Number(e.target.value))}
            />
          </label>
          <label>
            Dim brightness (%)
            <input
              type="number"
              min={1}
              max={100}
              value={s.idle_dim_pct}
              onChange={(e) => field("idle_dim_pct", Number(e.target.value))}
            />
          </label>
          <label>
            Idle clock (s)
            <input
              type="number"
              min={0}
              max={86400}
              value={s.idle_clock_s}
              onChange={(e) => field("idle_clock_s", Number(e.target.value))}
            />
          </label>
          <label>
            Idle dim 2 (s)
            <input
              type="number"
              min={0}
              max={65535}
              value={s.idle_dim2_s}
              onChange={(e) => field("idle_dim2_s", Number(e.target.value))}
            />
          </label>
          <label>
            Dim 2 brightness (%)
            <input
              type="number"
              min={1}
              max={100}
              value={s.idle_dim2_pct}
              onChange={(e) => field("idle_dim2_pct", Number(e.target.value))}
            />
          </label>
          <label>
            Screen off (s)
            <input
              type="number"
              min={0}
              max={65535}
              value={s.idle_off_s}
              onChange={(e) => field("idle_off_s", Number(e.target.value))}
            />
          </label>
          <label>
            Clock font size
            <select
              value={String(s.clock_font_px)}
              onChange={(e) => field("clock_font_px", Number(e.target.value))}
            >
              {[48, 72, 96, 128, 160].map((n) => (
                <option key={n} value={n}>
                  {n} px
                </option>
              ))}
            </select>
          </label>
        </div>

        <p className="hint">
          Defaults: dim 30s @ 30% → clock 120s → dim2 300s @ 30% → off 1800s. Set a timeout to 0 to
          disable that stage.
        </p>

        <div className="row" style={{ justifyContent: "flex-start" }}>
          <button type="button" className="ghost" onClick={() => void load()} disabled={busy}>
            Reload
          </button>
          <button type="button" onClick={() => void save()} disabled={busy}>
            Save &amp; restart board
          </button>
        </div>
        {msg && <div className={`msg ${msg.ok ? "ok" : "err"}`}>{msg.text}</div>}
      </div>
    </section>
  );
}
