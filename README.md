
# Luma Tools — Universal Media Toolkit

A powerful, modern media toolkit with a C++17 backend and a clean glassmorphism UI.  
Download from 10+ platforms and process files with 34 built-in tools — all from your browser.

![C++](https://img.shields.io/badge/Backend-C%2B%2B17-blue?logo=cplusplus)
![yt-dlp](https://img.shields.io/badge/Engine-yt--dlp-red)
![FFmpeg](https://img.shields.io/badge/Processing-FFmpeg-green)
![License](https://img.shields.io/badge/License-MIT-yellow)

---

## Features

### Media Downloader

Paste any URL and it auto-detects the platform, giving you the right download options.

| Platform    | MP3 | MP4 | Quality Selection |
|-------------|-----|-----|-------------------|
| YouTube     | ✅  | ✅  | ✅                |
| TikTok      | ✅  | ✅  | ✅                |
| Instagram   | ✅  | ✅  | ✅                |
| Spotify     | ✅  | —   | —                 |
| SoundCloud  | ✅  | —   | —                 |
| X / Twitter | ✅  | ✅  | ✅                |
| Facebook    | ✅  | ✅  | ✅                |
| Twitch      | ✅  | ✅  | ✅                |
| Vimeo       | ✅  | ✅  | ✅                |
| Reddit      | ✅  | ✅  | ✅                |
| + many more via yt-dlp |

### File Processing Tools

Each tool in the sidebar is labelled **In your browser** (runs locally via WebAssembly) or **On our server** (uses backend processing). Files sent to the server are processed and immediately deleted.

#### Image
| Tool                     | Where        | Description                                      |
|--------------------------|--------------|--------------------------------------------------|
| Image Compress           | Browser      | Reduce image file size (JPG, PNG, WebP)          |
| Image Resize             | Browser      | Scale images by width/height                     |
| Image Convert            | Browser      | Convert between PNG, JPG, WebP, BMP, TIFF        |
| Image Crop               | Browser      | Crop images with aspect ratio presets            |
| Favicon Generator        | Browser      | Create favicons from any image                   |
| Privacy Redaction        | Browser      | Redact/blur sensitive areas from images          |
| Background Remover       | Server       | Remove image backgrounds (AI-powered)            |
| Metadata Strip           | Server       | Remove EXIF and metadata from images             |

#### Video
| Tool                     | Where        | Description                                      |
|--------------------------|--------------|--------------------------------------------------|
| Video Compress           | Server       | Reduce video size (light/medium/heavy presets)   |
| Video Trim               | Server       | Cut video segments (fast or frame-precise mode)  |
| Video Convert            | Server       | Convert between MP4, WebM, MKV, AVI, MOV         |
| Extract Audio            | Server       | Rip audio track from any video file              |
| Video to GIF             | Server       | Convert video clips to animated GIF              |
| GIF to Video             | Server       | Convert GIF to MP4                               |
| Remove Audio             | Server       | Strip the audio track from a video               |
| Speed Change             | Server       | Speed up or slow down video                      |
| Frame Extract            | Server       | Extract individual frames as images              |
| Stabilize                | Server       | Reduce camera shake from video footage           |
| Subtitles                | Server       | Extract subtitle tracks from video files         |

#### Audio
| Tool                     | Where        | Description                                      |
|--------------------------|--------------|--------------------------------------------------|
| Audio Convert            | Browser      | Convert between MP3, AAC, WAV, FLAC, OGG, WMA   |
| Audio Normalise          | Server       | Normalise audio loudness levels                  |

#### Document
| Tool                     | Where        | Description                                      |
|--------------------------|--------------|--------------------------------------------------|
| PDF Compress             | Server       | Shrink PDF files via Ghostscript                 |
| PDF Merge                | Server       | Combine multiple PDFs into one                   |
| PDF to Images            | Server       | Convert PDF pages to PNG, JPG, or TIFF           |
| Images to PDF            | Server       | Combine images into a single PDF                 |

#### Utility
| Tool                     | Where        | Description                                      |
|--------------------------|--------------|--------------------------------------------------|
| QR Code                  | Browser      | Generate QR codes from any text or URL           |
| Hash Generator           | Server       | Compute file hashes (MD5, SHA1, SHA256, etc.)    |
| Base64                   | Browser      | Encode or decode text as Base64                  |
| JSON Formatter           | Browser      | Format, minify, and validate JSON                |
| Color Converter          | Browser      | Convert between HEX, RGB, HSL, HSV, CMYK        |
| Markdown Preview         | Browser      | Live markdown editor with HTML export            |
| Difference Checker       | Browser      | Line-by-line text/code diff (split or unified)   |
| Word Counter             | Browser      | Count words, characters, sentences, paragraphs   |

---

## UI/UX Highlights

- **Sidebar search** (`Ctrl+K`) to instantly filter tools
- **Smart drag & drop** — drop files anywhere, auto-routes to the right tool
- **Batch processing** — drop multiple files, get a single .zip back
- **Location labels** — every tool shows whether it runs in-browser or on-server
- **Animated background particles**
- **Responsive design** — works on desktop and mobile
- **PWA** — installable as a desktop or mobile app

---

## Prerequisites

1. **C++ Compiler** — MSVC (Visual Studio 2019+), GCC, or Clang with C++17 support
2. **CMake** ≥ 3.14 — [Download](https://cmake.org/download/)
3. **yt-dlp** — `pip install yt-dlp` or [GitHub releases](https://github.com/yt-dlp/yt-dlp/releases)
4. **FFmpeg** — [Download](https://ffmpeg.org/download.html)  
   *Required for all media processing and audio extraction*
5. **rembg** *(optional, for AI background removal)* — `pip install rembg`
6. **Ghostscript** *(optional, for PDF tools)* — [Download](https://www.ghostscript.com/releases/gsdnld.html)

---

## Quick Start

### Windows

```powershell
# 1. Install dependencies
pip install yt-dlp rembg
winget install Gyan.FFmpeg

# 2. Build
.\build.bat

# 3. Run
.\run.bat
```

### Linux / macOS

```bash
# 1. Install dependencies
pip3 install yt-dlp rembg
sudo apt install ffmpeg cmake g++   # Ubuntu/Debian
brew install ffmpeg cmake           # macOS

# 2. Build & Run
chmod +x build.sh run.sh
./build.sh
./run.sh
```

Then open **http://localhost:8080** in your browser.

---

## VPS / Production Deployment

A Windows Server deployment script is included:

```powershell
.\deploy\deploy-windows.ps1
```

This script handles:
- Installing CMake, FFmpeg, yt-dlp, rembg, and Ghostscript
- Building the project
- Setting up Caddy as a reverse proxy with automatic HTTPS
- Registering Windows services via NSSM (LumaTools + LumaToolsCaddy)

Additional deploy helpers:
- `deploy/restart.bat` — Restart services
- `deploy/status.bat` — Check service status

---

## Analytics Dashboard

Luma Tools includes a built-in analytics dashboard at `/stats` — no external services required.

### What it tracks

- **Tool uses** — every server-side tool invocation with the tool name
- **Downloads** — each download start with the detected platform
- **Unique visitors** — by hashed IP (FNV hash, raw IPs are never stored)
- **Custom events** — Ko-fi clicks and GitHub link clicks from the frontend

### Setup

Set the `STATS_PASSWORD` environment variable before starting the server. The dashboard is **disabled** if this variable is not set.

**Local / dev:**
```powershell
$env:STATS_PASSWORD = "your-password"
.\run.bat
```

**NSSM (Windows Service):**
1. Open NSSM: `nssm edit LumaTools`
2. Go to the **Environment** tab
3. Add: `STATS_PASSWORD=your-password`
4. Click *Edit service* then restart: `nssm restart LumaTools`

**Linux systemd:**
```ini
# /etc/systemd/system/luma-tools.service
[Service]
Environment="STATS_PASSWORD=your-password"
```
Then `systemctl daemon-reload && systemctl restart luma-tools`.

### Using the dashboard

1. Navigate to `https://your-domain/stats`
2. Enter the password you configured
3. Use the range buttons (**Today / 7 Days / 30 Days / All Time**) to switch time windows
4. Click **Send Digest** to push a daily summary to Discord immediately

The dashboard shows:
- **Summary cards** — total requests, tool uses, downloads, unique visitors, errors
- **Line chart** — requests per day over the selected range
- **Doughnut chart** — distribution of tool usage
- **Bar chart** — top download platforms
- **Top tools table** — ranked tool usage with in-line bar graphs
- **Events table** — tracked frontend events (Ko-fi clicks, GitHub link clicks)

### Daily Discord digest

At UTC midnight the server automatically posts a digest embed to your Discord webhook (if configured) containing today's totals, top tools, top platforms, unique visitors, and a 7-day rolling total.

### Data storage

Stats are stored as a JSON-lines file (`stats.jsonl`) alongside the processing directory — no database required. Each record is one line of JSON:

```json
{"ts":1739900000,"kind":"tool","name":"Video Compress","ok":true,"vh":"a3f9c12b4e"}
{"ts":1739900001,"kind":"event","name":"kofi_click"}
```

---

## Architecture

```
luma-tools/
├── src/
│   ├── headers/
│   │   ├── common.h           # Shared includes, globals, utility declarations
│   │   ├── discord.h          # Discord webhook function declarations
│   │   ├── stats.h            # Stats tracking API declarations
│   │   └── routes.h           # Route registration declarations
│   ├── main.cpp               # Server init, executable discovery, startup
│   ├── common.cpp             # Utility functions, download/job managers
│   ├── platform.cpp           # Platform detection (YouTube, TikTok, etc.)
│   ├── discord.cpp            # Discord webhook logging (rich embeds)
│   ├── stats.cpp              # Stats recording, querying, daily digest scheduler
│   ├── routes_download.cpp    # Download API endpoints
│   ├── routes_tools.cpp       # File processing tool endpoints
│   └── routes_stats.cpp       # Stats dashboard + analytics API endpoints
├── public/
│   ├── index.html             # Main page (sidebar + tool panels)
│   ├── styles.css             # Glassmorphism dark theme
│   └── js/
│       ├── state.js           # Shared app state
│       ├── utils.js           # Utility helpers (toast, formatting)
│       ├── ui.js              # Sidebar, search, drag & drop UI
│       ├── file-tools.js      # Upload zones and file handling
│       ├── tools-misc.js      # Browser-side tools (QR, Base64, JSON, Color, etc.)
│       ├── downloader.js      # Media downloader logic
│       ├── batch.js           # Batch processing queue
│       ├── wasm.js            # FFmpeg.wasm integration
│       ├── waveform.js        # Audio waveform visualiser
│       ├── crop.js            # Image crop canvas
│       ├── redact.js          # Privacy redaction canvas
│       ├── health.js          # Server health ticker
│       └── pwa.js             # PWA install prompt + background particles
│                                (also contains Ko-fi/GitHub click tracking)
├── deploy/
│   ├── deploy-windows.ps1     # Full VPS deployment script
│   ├── restart.bat            # Service restart helper
│   └── status.bat             # Service status helper
├── CMakeLists.txt             # Build config (auto-fetches dependencies)
├── build.bat / build.sh       # Build scripts
└── run.bat / run.sh           # Run scripts
```

---

## API Endpoints

### Stats (Dashboard)

| Method | Endpoint                  | Auth     | Description                                      |
|--------|---------------------------|----------|--------------------------------------------------|
| GET    | `/stats`                  | Cookie   | Analytics dashboard (login page if not authed)   |
| POST   | `/stats/login`            | —        | Submit password, sets `stats_auth` cookie        |
| GET    | `/stats/logout`           | —        | Clear auth cookie                                |
| GET    | `/api/stats`              | Required | JSON summary (`total`, `failures`, `by_name`)    |
| GET    | `/api/stats/timeseries`   | Required | Day-by-day counts (`?range=today\|week\|month\|all&kind=`) |
| GET    | `/api/stats/visitors`     | Required | Unique visitor count for range                   |
| GET    | `/api/stats/events`       | Required | Aggregated custom event counts                   |
| POST   | `/api/stats/event`        | **None** | Record a client-side event (public, CORS-open)   |
| POST   | `/api/stats/digest`       | Required | Trigger an immediate Discord digest              |

> **`POST /api/stats/event`** is intentionally public so the frontend can call it from user browsers without authentication.

### Downloads

| Method | Endpoint                | Description                       |
|--------|-------------------------|-----------------------------------|
| POST   | `/api/detect`           | Detect platform from URL          |
| POST   | `/api/analyze`          | Get media info (title, formats)   |
| POST   | `/api/download`         | Start a download                  |
| GET    | `/api/resolve-title`    | Resolve title from URL            |
| GET    | `/api/status/:id`       | Check download progress           |
| GET    | `/api/health`           | Server health, versions, git info |

### File Processing Tools

| Method | Endpoint                           | Description                  |
|--------|------------------------------------|------------------------------|
| POST   | `/api/tools/image-compress`        | Compress an image            |
| POST   | `/api/tools/image-resize`          | Resize an image              |
| POST   | `/api/tools/image-convert`         | Convert image format         |
| POST   | `/api/tools/image-crop`            | Crop an image                |
| POST   | `/api/tools/image-bg-remove`       | Remove image background      |
| POST   | `/api/tools/metadata-strip`        | Strip image metadata         |
| POST   | `/api/tools/audio-convert`         | Convert audio format         |
| POST   | `/api/tools/audio-normalize`       | Normalise audio loudness     |
| POST   | `/api/tools/video-compress`        | Compress video (async)       |
| POST   | `/api/tools/video-trim`            | Trim video (async)           |
| POST   | `/api/tools/video-convert`         | Convert video (async)        |
| POST   | `/api/tools/video-extract-audio`   | Extract audio (async)        |
| POST   | `/api/tools/video-to-gif`          | Convert video to GIF (async) |
| POST   | `/api/tools/gif-to-video`          | Convert GIF to video (async) |
| POST   | `/api/tools/video-remove-audio`    | Remove audio track (async)   |
| POST   | `/api/tools/video-speed`           | Change video speed (async)   |
| POST   | `/api/tools/video-frame`           | Extract video frame (async)  |
| POST   | `/api/tools/video-stabilize`       | Stabilize video (async)      |
| POST   | `/api/tools/subtitle-extract`      | Extract subtitles (async)    |
| POST   | `/api/tools/pdf-compress`          | Compress a PDF               |
| POST   | `/api/tools/pdf-merge`             | Merge multiple PDFs          |
| POST   | `/api/tools/pdf-to-images`         | Convert PDF to images        |
| POST   | `/api/tools/images-to-pdf`         | Convert images to PDF        |
| POST   | `/api/tools/favicon-generate`      | Generate favicon             |
| POST   | `/api/tools/hash-generate`         | Generate file hash           |
| GET    | `/api/tools/status/:id`            | Check async job progress     |
| GET    | `/api/tools/result/:id`            | Download processed file      |

---

## Environment Variables

| Variable         | Default | Description                                                                   |
|------------------|---------|-------------------------------------------------------------------------------|
| `PORT`           | `8080`  | Server listen port                                                            |
| `STATS_PASSWORD` | *(none)*| Password for the `/stats` dashboard. **Dashboard is disabled if not set.**    |
| `DISCORD_WEBHOOK_URL` | *(none)*| Discord webhook URL for tool/download notifications and daily digest.  |

---

## License

[Business Source License 1.1](LICENSE) — personal, non-commercial use only.
Converts to AGPL v3 on **February 18, 2031**.

