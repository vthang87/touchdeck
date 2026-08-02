import { useCallback, useEffect, useState } from "react";
import { boardRequest } from "./boardApi";

type TileTarget = { kind: string; value: string };

type GridTile = {
  id: string;
  label: string;
  icon: string;
  color: string;
  action: string;
  action_id?: string;
  target?: TileTarget;
};

type ShortcutPage = {
  cols: number;
  rows: number;
  tiles: GridTile[];
};

type DeckProfile = {
  rev: number;
  page_count: number;
  pages: ShortcutPage[];
};

const ACTIONS = [
  "volume_up",
  "volume_down",
  "mute",
  "play_pause",
  "next",
  "previous",
  "app",
];

const ICONS = [
  "vol_up",
  "vol_down",
  "mute",
  "play",
  "pause",
  "next",
  "prev",
  "shuffle",
  "power",
  "settings",
  "home",
  "bell",
  "mail",
  "wifi",
  "file",
  "folder",
  "gpt",
  "codex",
  "cursor",
  "iterm",
  "terminal",
  "vscode",
  "slack",
  "telegram",
  "safari",
  "chrome",
  "finder",
  "music",
  "messages",
  "app",
];

const COLORS = [
  "#475569",
  "#64748B",
  "#0F766E",
  "#0369A1",
  "#BE123C",
  "#16A34A",
  "#7C3AED",
  "#10A37F",
  "#6366F1",
  "#1F2937",
  "#007ACC",
  "#4A154B",
  "#229ED9",
];

const PRESETS = [
  { name: "ChatGPT", icon: "gpt", color: "#10A37F", action_id: "open_chatgpt", value: "com.openai.chat" },
  { name: "Codex", icon: "codex", color: "#0D8A6A", action_id: "open_codex", value: "com.openai.codex" },
  {
    name: "Cursor",
    icon: "cursor",
    color: "#6366F1",
    action_id: "open_cursor",
    value: "com.todesktop.230313mzl4w4u92",
  },
  { name: "iTerm", icon: "iterm", color: "#1F2937", action_id: "open_iterm", value: "com.googlecode.iterm2" },
  {
    name: "VS Code",
    icon: "vscode",
    color: "#007ACC",
    action_id: "open_vscode",
    value: "com.microsoft.VSCode",
  },
  {
    name: "Slack",
    icon: "slack",
    color: "#4A154B",
    action_id: "open_slack",
    value: "com.tinyspeck.slackmacgap",
  },
  {
    name: "Telegram",
    icon: "telegram",
    color: "#229ED9",
    action_id: "open_telegram",
    value: "ru.keepcoder.Telegram",
  },
  { name: "Safari", icon: "safari", color: "#0369A1", action_id: "open_safari", value: "com.apple.Safari" },
];

function emptyTile(i: number): GridTile {
  return {
    id: `tile_${i}`,
    label: `Tile ${i + 1}`,
    color: COLORS[i % COLORS.length],
    icon: "app",
    action: "app",
    action_id: "open_safari",
    target: { kind: "bundle", value: "com.apple.Safari" },
  };
}

function emptyPage(): ShortcutPage {
  return { cols: 4, rows: 2, tiles: [] };
}

function ensureTiles(page: ShortcutPage): ShortcutPage {
  const n = page.cols * page.rows;
  const tiles = [...page.tiles];
  while (tiles.length < n) tiles.push(emptyTile(tiles.length));
  return { ...page, tiles: tiles.slice(0, n) };
}

function normalizeDeck(raw: Partial<DeckProfile> & { cols?: number; rows?: number; tiles?: GridTile[] }): DeckProfile {
  if (Array.isArray(raw.pages) && raw.pages.length > 0) {
    let page_count = raw.page_count ?? raw.pages.length + 1;
    if (page_count < 2) page_count = 2;
    if (page_count > 4) page_count = 4;
    const want = page_count - 1;
    const pages = raw.pages.slice(0, want).map(ensureTiles);
    while (pages.length < want) pages.push(ensureTiles(emptyPage()));
    return { rev: raw.rev ?? 1, page_count, pages };
  }
  return {
    rev: raw.rev ?? 1,
    page_count: 2,
    pages: [
      ensureTiles({
        cols: raw.cols ?? 4,
        rows: raw.rows ?? 2,
        tiles: raw.tiles ?? [],
      }),
    ],
  };
}

type Props = {
  host: string;
  onLog?: (level: string, message: string) => void;
};

export default function GridPanel({ host, onLog }: Props) {
  const [deck, setDeck] = useState<DeckProfile>({ rev: 1, page_count: 2, pages: [emptyPage()] });
  const [editIdx, setEditIdx] = useState(0);
  const [msg, setMsg] = useState<{ text: string; ok: boolean } | null>(null);
  const [busy, setBusy] = useState(false);
  const [dragFrom, setDragFrom] = useState<number | null>(null);

  const page = deck.pages[Math.min(editIdx, Math.max(0, deck.pages.length - 1))] ?? emptyPage();

  const load = useCallback(async () => {
    setBusy(true);
    setMsg(null);
    try {
      const raw = (await boardRequest("/api/grid", { host })) as Record<string, unknown>;
      const next = normalizeDeck(raw as never);
      setDeck(next);
      setEditIdx(0);
      setMsg({ text: `Loaded rev ${next.rev} · ${next.page_count} pages`, ok: true });
      onLog?.("info", `Deck loaded rev ${next.rev} pages=${next.page_count}`);
    } catch (e) {
      setMsg({ text: String(e), ok: false });
      onLog?.("error", `Grid load failed: ${e}`);
    } finally {
      setBusy(false);
    }
  }, [host, onLog]);

  useEffect(() => {
    void load();
  }, [load]);

  function updatePage(patch: Partial<ShortcutPage>) {
    setDeck((d) => {
      const pages = d.pages.map((p, i) => (i === editIdx ? ensureTiles({ ...p, ...patch }) : p));
      return { ...d, pages };
    });
  }

  function updateTile(idx: number, patch: Partial<GridTile>) {
    setDeck((d) => {
      const pages = d.pages.map((p, pi) => {
        if (pi !== editIdx) return p;
        const tiles = p.tiles.map((t, i) => (i === idx ? { ...t, ...patch } : t));
        return ensureTiles({ ...p, tiles });
      });
      return { ...d, pages };
    });
  }

  function setPageCount(count: number) {
    setDeck((d) => {
      let page_count = count;
      if (page_count < 2) page_count = 2;
      if (page_count > 4) page_count = 4;
      const want = page_count - 1;
      const pages = [...d.pages];
      while (pages.length < want) pages.push(ensureTiles(emptyPage()));
      return { ...d, page_count, pages: pages.slice(0, want).map(ensureTiles) };
    });
    setEditIdx((i) => Math.min(i, Math.max(0, count - 2)));
  }

  function applyPreset(idx: number, name: string) {
    const p = PRESETS.find((x) => x.name === name);
    if (!p) return;
    updateTile(idx, {
      id: p.name.toLowerCase().replace(/[^a-z0-9]+/g, "_"),
      label: p.name,
      icon: p.icon,
      color: p.color,
      action: "app",
      action_id: p.action_id,
      target: { kind: "bundle", value: p.value },
    });
  }

  function moveTile(from: number, to: number) {
    if (from === to) return;
    setDeck((d) => {
      const pages = d.pages.map((p, pi) => {
        if (pi !== editIdx) return p;
        const tiles = [...p.tiles];
        const [item] = tiles.splice(from, 1);
        tiles.splice(to, 0, item);
        return { ...p, tiles };
      });
      return { ...d, pages };
    });
  }

  async function save() {
    setBusy(true);
    const next: DeckProfile = {
      ...deck,
      rev: (deck.rev || 0) + 1,
      pages: deck.pages.map(ensureTiles),
    };
    try {
      const body = "json=" + encodeURIComponent(JSON.stringify(next));
      const j = (await boardRequest("/api/grid", {
        host,
        method: "POST",
        body,
      })) as { grid?: Record<string, unknown> };
      const saved = normalizeDeck((j.grid || next) as never);
      setDeck(saved);
      setMsg({ text: `Saved rev ${saved.rev}`, ok: true });
      onLog?.("info", `Deck saved rev ${saved.rev} pages=${saved.page_count}`);
    } catch (e) {
      setMsg({ text: String(e), ok: false });
      onLog?.("error", `Grid save failed: ${e}`);
    } finally {
      setBusy(false);
    }
  }

  async function reset() {
    setBusy(true);
    try {
      const j = (await boardRequest("/api/grid/reset", {
        host,
        method: "POST",
      })) as { grid?: Record<string, unknown> };
      if (!j.grid) throw new Error("No grid in response");
      const saved = normalizeDeck(j.grid as never);
      setDeck(saved);
      setEditIdx(0);
      setMsg({ text: "Reset to defaults", ok: true });
      onLog?.("info", "Deck reset to defaults");
    } catch (e) {
      setMsg({ text: String(e), ok: false });
      onLog?.("error", `Grid reset failed: ${e}`);
    } finally {
      setBusy(false);
    }
  }

  return (
    <section className="panel">
      <h2>Grid</h2>
      <p className="hint" style={{ marginTop: 0 }}>
        Page 0 is Media (fixed UI). Edit shortcut pages below. Match <code>action_id</code> to Profile
        mappings.
      </p>

      <div className="row" style={{ justifyContent: "flex-start" }}>
        <label className="inline-field">
          Total pages
          <select
            value={deck.page_count}
            onChange={(e) => setPageCount(Number(e.target.value))}
          >
            {[2, 3, 4].map((n) => (
              <option key={n} value={n}>
                {n} (Media + {n - 1} grid{n > 2 ? "s" : ""})
              </option>
            ))}
          </select>
        </label>
        <label className="inline-field">
          Edit shortcut
          <select value={editIdx} onChange={(e) => setEditIdx(Number(e.target.value))}>
            {deck.pages.map((_, i) => (
              <option key={i} value={i}>
                Shortcuts {i + 1} (device page {i + 1})
              </option>
            ))}
          </select>
        </label>
        <label className="inline-field">
          Columns
          <select
            value={page.cols}
            onChange={(e) => updatePage({ cols: Number(e.target.value) })}
          >
            {[2, 3, 4, 5].map((n) => (
              <option key={n} value={n}>
                {n}
              </option>
            ))}
          </select>
        </label>
        <label className="inline-field">
          Rows
          <select
            value={page.rows}
            onChange={(e) => updatePage({ rows: Number(e.target.value) })}
          >
            {[1, 2, 3].map((n) => (
              <option key={n} value={n}>
                {n}
              </option>
            ))}
          </select>
        </label>
        <button type="button" className="ghost" onClick={() => void load()} disabled={busy}>
          Reload
        </button>
        <button type="button" onClick={() => void save()} disabled={busy}>
          Save deck
        </button>
        <button type="button" className="danger" onClick={() => void reset()} disabled={busy}>
          Reset defaults
        </button>
      </div>
      {msg && <div className={`msg ${msg.ok ? "ok" : "err"}`}>{msg.text}</div>}

      <div
        className="tile-grid"
        style={{ gridTemplateColumns: `repeat(${page.cols}, minmax(160px, 1fr))` }}
      >
        {page.tiles.map((t, idx) => (
          <div
            key={`${t.id}-${idx}`}
            className={`tile-card${dragFrom === idx ? " dragging" : ""}`}
            onDragOver={(e) => {
              e.preventDefault();
            }}
            onDrop={(e) => {
              e.preventDefault();
              const from = Number(e.dataTransfer.getData("text/plain"));
              if (Number.isFinite(from)) moveTile(from, idx);
              setDragFrom(null);
            }}
          >
            <div className="tile-head">
              <span
                className="tile-handle"
                draggable
                title="Drag to reorder"
                onDragStart={(e) => {
                  e.dataTransfer.effectAllowed = "move";
                  e.dataTransfer.setData("text/plain", String(idx));
                  setDragFrom(idx);
                }}
                onDragEnd={() => setDragFrom(null)}
              >
                ⋮⋮
              </span>
              <strong>
                #{idx + 1} {t.id}
              </strong>
            </div>

            <label>
              preset
              <select defaultValue="" onChange={(e) => applyPreset(idx, e.target.value)}>
                <option value="">— app preset —</option>
                {PRESETS.map((p) => (
                  <option key={p.name} value={p.name}>
                    {p.name}
                  </option>
                ))}
              </select>
            </label>
            <label>
              id
              <input value={t.id} onChange={(e) => updateTile(idx, { id: e.target.value })} />
            </label>
            <label>
              label
              <input
                value={t.label}
                onChange={(e) => updateTile(idx, { label: e.target.value })}
              />
            </label>
            <label>
              action_id
              <input
                value={t.action_id ?? ""}
                onChange={(e) => updateTile(idx, { action_id: e.target.value })}
                placeholder="open_vscode"
              />
            </label>
            <label>
              icon
              <select value={t.icon} onChange={(e) => updateTile(idx, { icon: e.target.value })}>
                {ICONS.map((i) => (
                  <option key={i} value={i}>
                    {i}
                  </option>
                ))}
              </select>
            </label>
            <label>
              color
              <select value={t.color} onChange={(e) => updateTile(idx, { color: e.target.value })}>
                {COLORS.map((c) => (
                  <option key={c} value={c}>
                    {c}
                  </option>
                ))}
              </select>
            </label>
            <label>
              action
              <select
                value={t.action}
                onChange={(e) => {
                  const action = e.target.value;
                  if (action === "app") {
                    updateTile(idx, {
                      action,
                      target: t.target ?? { kind: "bundle", value: "" },
                    });
                  } else {
                    updateTile(idx, { action, target: undefined });
                  }
                }}
              >
                {ACTIONS.map((a) => (
                  <option key={a} value={a}>
                    {a}
                  </option>
                ))}
              </select>
            </label>
            {t.action === "app" && (
              <>
                <label>
                  target kind
                  <select
                    value={t.target?.kind ?? "bundle"}
                    onChange={(e) =>
                      updateTile(idx, {
                        target: {
                          kind: e.target.value,
                          value: t.target?.value ?? "",
                        },
                      })
                    }
                  >
                    <option value="bundle">bundle</option>
                    <option value="path">path</option>
                  </select>
                </label>
                <label>
                  bundle / path
                  <input
                    value={t.target?.value ?? ""}
                    onChange={(e) =>
                      updateTile(idx, {
                        target: {
                          kind: t.target?.kind ?? "bundle",
                          value: e.target.value,
                        },
                      })
                    }
                    placeholder="com.apple.Safari"
                  />
                </label>
              </>
            )}
          </div>
        ))}
      </div>
    </section>
  );
}
