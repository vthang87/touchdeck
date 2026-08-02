/**
 * TouchDeck web installer — static assets + runtime manifest for GitHub Releases.
 */

export interface Env {
  ASSETS: Fetcher;
  /** e.g. vthang87/touchdeck — builds releases/latest/download URL */
  GITHUB_REPOSITORY?: string;
  /** Override firmware base URL (optional) */
  FIRMWARE_BASE_URL?: string;
}

function firmwareBaseUrl(env: Env): string | null {
  if (env.FIRMWARE_BASE_URL) {
    const base = env.FIRMWARE_BASE_URL.trim();
    return base.endsWith("/") ? base : `${base}/`;
  }
  if (env.GITHUB_REPOSITORY) {
    return `https://github.com/${env.GITHUB_REPOSITORY}/releases/latest/download/`;
  }
  return null;
}

function withHeaders(response: Response, extra: Record<string, string> = {}): Response {
  const headers = new Headers(response.headers);
  for (const [key, value] of Object.entries(extra)) {
    headers.set(key, value);
  }
  headers.set("X-Content-Type-Options", "nosniff");
  return new Response(response.body, {
    status: response.status,
    statusText: response.statusText,
    headers,
  });
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    if (request.method === "OPTIONS") {
      return new Response(null, {
        headers: {
          "Access-Control-Allow-Origin": "*",
          "Access-Control-Allow-Methods": "GET, HEAD, OPTIONS",
          "Access-Control-Allow-Headers": "Content-Type",
        },
      });
    }

    if (url.pathname === "/manifest.json") {
      const assetRes = await env.ASSETS.fetch(new URL("/manifest.json", request.url));
      if (!assetRes.ok) {
        return assetRes;
      }
      const manifest = (await assetRes.json()) as Record<string, unknown>;
      const base = firmwareBaseUrl(env);
      if (base) {
        manifest.firmwareBaseUrl = base;
      }
      return Response.json(manifest, {
        headers: {
          "Cache-Control": "no-store",
          "Access-Control-Allow-Origin": "*",
        },
      });
    }

    const assetResponse = await env.ASSETS.fetch(request);
    if (assetResponse.status === 404 && !url.pathname.includes(".")) {
      const index = await env.ASSETS.fetch(new URL("/index.html", request.url));
      if (index.ok) {
        return withHeaders(index, { "Content-Type": "text/html; charset=utf-8" });
      }
    }
    return withHeaders(assetResponse);
  },
};
