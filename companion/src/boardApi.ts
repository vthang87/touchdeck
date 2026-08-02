import { invoke } from "@tauri-apps/api/core";

export type BoardResponse = {
  ok: boolean;
  status: number;
  text: string;
  error?: string | null;
};

export async function boardRequest(
  path: string,
  options: { method?: string; body?: string; host?: string } = {},
): Promise<unknown> {
  const resp = await invoke<BoardResponse>("board_request", {
    path,
    method: options.method ?? "GET",
    body: options.body ?? "",
    host: options.host ?? null,
  });
  if (resp.error) {
    throw new Error(resp.error);
  }
  if (!resp.ok) {
    throw new Error(resp.text || `HTTP ${resp.status}`);
  }
  if (!resp.text) return {};
  try {
    return JSON.parse(resp.text);
  } catch {
    throw new Error(`Invalid board response (HTTP ${resp.status})`);
  }
}

export async function getBoardHost(): Promise<string> {
  return invoke<string>("get_board_host");
}

export async function setBoardHost(host: string): Promise<void> {
  await invoke("set_board_host", { host });
}
