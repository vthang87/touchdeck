# Cloudflare Worker — TouchDeck Web Installer

Deploy trang cài firmware ESP (`web/install/`) lên **Cloudflare Workers** với static assets + Worker inject `firmwareBaseUrl` từ GitHub Releases.

## Kiến trúc

```text
web/install/          # Source HTML/JS/manifest
       │
       ▼ sync-web-installer.sh
web/worker/public/    # Static assets uploaded with Worker
web/worker/src/index.ts   # Serves assets; patches /manifest.json at runtime
```

Worker URL mặc định: `https://touchdeck-installer.<account>.workers.dev`

## Yêu cầu

| Thành phần | Chi tiết |
|---|---|
| Tài khoản Cloudflare | Free tier đủ cho static installer |
| Wrangler | v4+ (cần **Node.js 22+**) |
| GitHub Secrets | `CLOUDFLARE_API_TOKEN`, `CLOUDFLARE_ACCOUNT_ID` |
| GitHub Release | Tag `v*` — Worker trỏ firmware tới `releases/latest/download` |

## Secrets GitHub (CI)

Tạo API token: Cloudflare Dashboard → **My Profile** → **API Tokens** → Create Token → template **Edit Cloudflare Workers**.

Repository → **Settings** → **Secrets and variables** → **Actions**:

| Secret | Giá trị |
|---|---|
| `CLOUDFLARE_API_TOKEN` | Token có quyền Workers Scripts Edit |
| `CLOUDFLARE_ACCOUNT_ID` | Account ID (Dashboard sidebar) |

Push `main` hoặc `dev` → workflow `.github/workflows/cloudflare.yml` tự deploy.

| Branch | Worker | URL |
|---|---|---|
| `main` | `touchdeck-installer` | `https://touchdeck-installer.<account>.workers.dev` |
| `dev` | `touchdeck-installer-dev` | `https://touchdeck-installer-dev.<account>.workers.dev` |

Chỉ hai branch này trigger deploy — branch khác và PR **không** deploy Worker.

## Deploy thủ công

```bash
# 1. Sync assets từ web/install
./scripts/sync-web-installer.sh

# 2. Đăng nhập Cloudflare (lần đầu)
cd web/worker
pnpm install
npx wrangler login

# 3. Deploy (main = production, dev = staging)
npx wrangler deploy --var GITHUB_REPOSITORY:vthang87/touchdeck
# hoặc dev:
npx wrangler deploy --env dev --var GITHUB_REPOSITORY:vthang87/touchdeck
```

Hoặc từ `web/`:

```bash
pnpm run sync
pnpm run worker:deploy
```

## Biến môi trường Worker

| Var | Mô tả |
|---|---|
| `GITHUB_REPOSITORY` | `owner/repo` → `https://github.com/owner/repo/releases/latest/download/` |
| `FIRMWARE_BASE_URL` | Override URL tải firmware (tuỳ chọn) |

CI set: `wrangler deploy --var GITHUB_REPOSITORY:${{ github.repository }}`

Local dev:

```bash
cd web/worker
pnpm run dev
# Worker dev server + manifest injection
```

## Custom domain (tuỳ chọn)

Trong Cloudflare Dashboard → Workers → **touchdeck-installer** → **Settings** → **Domains & Routes** → Add `install.example.com`.

Hoặc thêm vào `wrangler.jsonc`:

```jsonc
"routes": [
  { "pattern": "install.example.com", "custom_domain": true }
]
```

## So sánh GitHub Pages vs Worker

| | GitHub Pages | Cloudflare Worker |
|---|---|---|
| Workflow | `pages.yml` | `cloudflare.yml` |
| Manifest | Patch tĩnh lúc build | Inject runtime qua Worker |
| CDN | GitHub | Cloudflare global |
| Custom domain | `*.github.io` | workers.dev hoặc domain riêng |

Có thể dùng **cả hai** — cùng source `web/install/`.

## Troubleshooting

| Vấn đề | Cách xử lý |
|---|---|
| Deploy CI fail: missing secrets | Thêm `CLOUDFLARE_API_TOKEN` + `CLOUDFLARE_ACCOUNT_ID` |
| Manifest không có `firmwareBaseUrl` | Set `GITHUB_REPOSITORY` hoặc `FIRMWARE_BASE_URL` |
| Flash không tải được .bin | Tạo GitHub Release (tag `v*`) trước |
| `wrangler login` hết hạn | Chạy lại `npx wrangler login` |

## Liên quan

- [`docs/web-install.md`](web-install.md) — quy trình flash, manifest, esptool-js
- [`.github/workflows/cloudflare.yml`](../.github/workflows/cloudflare.yml) — CI deploy
