export {};

declare global {
  interface Window {
    touchdeck: {
      launchApp: (target: {
        kind: "bundle" | "path";
        value: string;
      }) => Promise<{ ok: boolean; error?: string }>;
      getVolume: () => Promise<{
        ok: boolean;
        level?: number;
        muted?: boolean;
        error?: string;
      }>;
      adjustVolume: (delta: 3 | -3) => Promise<{
        ok: boolean;
        level?: number;
        muted?: boolean;
        error?: string;
      }>;
      refreshVolume: () => Promise<boolean>;
      onHostVolume: (cb: (v: { level: number; muted: boolean }) => void) => void;
      onApprovalUpdate: (
        cb: (payload: {
          pending: Array<{ id: string; source: string; title: string; body: string }>;
        }) => void,
      ) => void;
      setConnectionStatus: (name: string) => Promise<boolean>;
      getDefaults: () => Promise<{ host: string; port: number }>;
      discoverDevices: () => Promise<{
        ok: boolean;
        devices: Array<{ name: string; host: string; port: number }>;
        error?: string;
      }>;
      boardRequest: (request: {
        host: string;
        path: string;
        method?: string;
        body?: string;
      }) => Promise<{ ok: boolean; status: number; text: string; error?: string }>;
      uploadBoardIcon: (request: {
        host: string;
        id: string;
        data: ArrayBuffer;
      }) => Promise<{ ok: boolean; status: number; text: string; error?: string }>;
      showError: (title: string, body: string) => Promise<void>;
    };
  }
}
