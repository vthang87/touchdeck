import * as fs from "fs";
import * as http from "http";
import * as path from "path";

const INSTALL_PORT = 8787;
const MIME: Record<string, string> = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".bin": "application/octet-stream",
  ".md": "text/plain; charset=utf-8",
};

let server: http.Server | null = null;

function contentType(filePath: string): string {
  return MIME[path.extname(filePath).toLowerCase()] || "application/octet-stream";
}

function resolveFile(root: string, urlPath: string): string | null {
  const safe = path.normalize(urlPath).replace(/^(\.\.[/\\])+/, "");
  const rel = safe === "/" || safe === "" ? "index.html" : safe.replace(/^\//, "");
  const file = path.join(root, rel);
  if (!file.startsWith(root)) return null;
  if (!fs.existsSync(file) || fs.statSync(file).isDirectory()) {
    if (rel === "index.html") return null;
    const index = path.join(root, "index.html");
    return fs.existsSync(index) ? index : null;
  }
  return file;
}

export function webInstallRoot(): string {
  return path.join(__dirname, "web-install");
}

export function webInstallUrl(): string {
  return `http://127.0.0.1:${INSTALL_PORT}/`;
}

export function startWebInstallServer(): boolean {
  const root = webInstallRoot();
  if (!fs.existsSync(root)) {
    console.warn(`[web-install] missing ${root} — run companion build with web/install copy`);
    return false;
  }
  if (server) return true;

  server = http.createServer((req, res) => {
    const file = resolveFile(root, req.url?.split("?")[0] || "/");
    if (!file) {
      res.writeHead(404, { "Content-Type": "text/plain" });
      res.end("Not found");
      return;
    }
    res.writeHead(200, {
      "Content-Type": contentType(file),
      "Cache-Control": "no-store",
      "Access-Control-Allow-Origin": "*",
    });
    fs.createReadStream(file).pipe(res);
  });

  server.listen(INSTALL_PORT, "127.0.0.1", () => {
    console.log(`[web-install] http://127.0.0.1:${INSTALL_PORT}/`);
  });
  server.on("error", (err) => {
    console.warn("[web-install] server error:", err.message);
    server = null;
  });
  return true;
}

export function stopWebInstallServer(): void {
  if (!server) return;
  server.close();
  server = null;
}
