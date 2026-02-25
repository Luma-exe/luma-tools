<div align="center">
  <img src="public/icon-512.png" width="96" /><br/><br/>

  <h1>Luma Tools</h1>

  <b>A free, open source toolkit for students, creators and developers</b><br/><br/>

  Download from 10+ platforms &nbsp;·&nbsp; 50+ file processing tools &nbsp;·&nbsp; AI study features &nbsp;·&nbsp; Runs in your browser

  <br/><br/>

  [![Live](https://img.shields.io/badge/LIVE-tools.lumaplayground.com-1a73e8?style=flat-square)](https://tools.lumaplayground.com)
  &nbsp;
  ![Version](https://img.shields.io/badge/VERSION-2.2.0-7c3aed?style=flat-square)
  &nbsp;
  ![C++](https://img.shields.io/badge/C%2B%2B-17-0057a8?style=flat-square&logo=cplusplus&logoColor=white)
  &nbsp;
  ![Tools](https://img.shields.io/badge/TOOLS-50%2B-16a34a?style=flat-square)
  &nbsp;
  ![License](https://img.shields.io/badge/LICENSE-BSL--1.1-ca8a04?style=flat-square)

</div>

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

Each tool is labelled **In your browser** (runs locally — nothing is sent to the server) or **On our server** (backend processing, files deleted immediately after).

#### Image
| Tool                | Where              | Description                                                    |
|---------------------|--------------------|----------------------------------------------------------------|
| Image Compress      | Browser            | Reduce image file size (JPG, PNG, WebP) via Canvas API         |
| Image Resize        | Browser            | Scale images by width/height via Canvas API                    |
| Image Convert       | Browser / Server   | Convert between PNG, JPG, WebP, BMP, TIFF; SVG rasterised server-side |
| Image Crop          | Browser            | Crop images with aspect ratio presets via Canvas API           |
| Image Watermark     | Server             | Burn a text watermark onto images (position, colour, opacity)  |
| Favicon Generator   | Browser            | Create multi-size favicons from any image                      |
| Privacy Redaction   | Browser            | Redact/blur sensitive areas from images via Canvas             |
| Background Remover  | Server             | Remove image backgrounds (rembg AI)                            |
| Metadata Strip      | Server             | Remove EXIF and all metadata from images                       |

#### Video
| Tool                | Where   | Description                                                   |
|---------------------|---------|---------------------------------------------------------------|
| Video Compress      | Server  | Reduce video size (light / medium / heavy presets)            |
| Video Trim          | Server  | Cut video segments (fast stream-copy or frame-precise mode)   |
| Video Convert       | Server  | Convert between MP4, WebM, MKV, AVI, MOV                     |
| Extract Audio       | Server  | Rip audio track from any video file                           |
| Video to GIF        | Server  | Convert video clips to animated GIF                           |
| GIF to Video        | Server  | Convert GIF to MP4                                            |
| Remove Audio        | Server  | Strip the audio track from a video                            |
| Speed Change        | Server  | Speed up or slow down video                                   |
| Frame Extract       | Server  | Extract individual frames as images                           |
| Stabilize           | Server  | Reduce camera shake from video footage                        |
| Subtitles           | Server  | Extract subtitle tracks from video files                      |
| Video Redaction     | Server  | Blur/black-out regions in video frames using ffmpeg drawbox   |

#### Audio
| Tool                | Where   | Description                                                   |
|---------------------|---------|---------------------------------------------------------------|
| Audio Convert       | Browser | Convert between MP3, AAC, WAV, FLAC, OGG, WMA via ffmpeg.wasm |
| Audio Trim          | Server  | Trim audio by time range (fast copy or precise re-encode)     |
| Audio Normalise     | Server  | Normalise audio loudness levels                               |

#### Document
| Tool                | Where   | Description                                                   |
|---------------------|---------|---------------------------------------------------------------|
| PDF Compress        | Server  | Shrink PDF files via Ghostscript                              |
| PDF Merge           | Server  | Combine multiple PDFs into one                                |
| PDF Split           | Server  | Extract a page range from a PDF via Ghostscript               |
| PDF to Images       | Server  | Convert PDF pages to PNG, JPG, or TIFF                        |
| Images to PDF       | Server  | Combine images into a single PDF                              |
| Markdown to PDF     | Server  | Convert Markdown (incl. Obsidian syntax) to PDF via Pandoc    |

#### Data
| Tool                | Where   | Description                                                   |
|---------------------|---------|---------------------------------------------------------------|
| CSV / JSON Convert  | Server  | Convert CSV → JSON or JSON → CSV                              |

#### AI *(requires `GROQ_API_KEY`)*
| Tool                | Where   | Description                                                                          |
|---------------------|---------|--------------------------------------------------------------------------------------|
| Study Notes         | Server  | Generate comprehensive university-grade study notes via a 3-pass AI pipeline         |
| Improve Notes       | Server  | Rewrite and expand existing notes based on AI coverage analysis feedback              |
| Flashcards          | Server  | Generate Q&A flashcard sets from any content                                         |
| Quiz Generator      | Server  | Create multiple-choice quizzes from any content                                      |
| Paraphrase          | Server  | Rewrite text in different tones/styles                                               |
| Coverage Analysis   | Server  | Analyse how well your notes cover the source material with a scored report           |
| Mind Map            | Server  | Generate a mind-map structure from text                                              |
| YouTube Summary     | Server  | Summarise a YouTube video by transcript                                              |

#### Utility
| Tool                | Where   | Description                                                   |
|---------------------|---------|---------------------------------------------------------------|
| QR Code             | Browser | Generate QR codes from any text or URL                        |
| Hash Generator      | Server  | Compute file hashes (MD5, SHA1, SHA256, SHA512)               |
| Archive Extractor   | Server  | Extract ZIP, 7Z, RAR, TAR, ISO, CAB, DMG and 20+ more formats |
| Citation Generator  | Server  | Generate citations (APA, MLA, Chicago) from URL or DOI        |
| Base64              | Browser | Encode or decode text as Base64                               |
| JSON Formatter      | Browser | Format, minify, and validate JSON                             |
| Color Converter     | Browser | Convert between HEX, RGB, HSL, HSV, CMYK                     |
| Markdown Preview    | Browser | Live markdown editor with HTML export                         |
| Difference Checker  | Browser | Line-by-line text/code diff (split or unified view)           |
| Word Counter        | Browser | Count words, characters, sentences, paragraphs                |

---

## How AI Study Notes Works

The Study Notes tool runs a **3-pass AI pipeline** to ensure notes are exhaustive and accurate — not just a surface summary.

### Pass 1 — Content Checklist Extraction
Before any notes are written, a fast model (`llama-3.1-8b-instant`) reads the full source material and extracts a **flat bullet checklist** of every topic, concept, formula, theorem, algorithm, worked example, and application present in the source. This becomes a binding contract: nothing on the list may be omitted.

### Pass 2 — Structured Note Generation
The main model (`llama-3.3-70b-versatile`, 8 192 token output limit) receives both the source text and the checklist. It is instructed to:
- Address **every item on the checklist** — missing even one is treated as a failure
- Re-work every worked example **step-by-step**, showing all intermediate algebra
- Define **every variable** in every formula with units where applicable
- Flag **exam hints** and common mistakes explicitly
- Include practical applications and connections to other topics
- Adapt to the detected subject area:
  - **Mathematics** — adds a key formulas summary section, enforces full step-by-step algebra
  - **Computer Science** — requires pseudocode, Big-O notation, edge case discussion
  - **Science** — enforces unit definitions, lab/practical connections

### Pass 3 — Auto-Refine
After generation, a second `llama-3.3-70b-versatile` call compares the draft notes against the checklist and silently fills any remaining gaps. The refined output replaces the draft only if it is at least 60% as long as the original (preventing truncated rewrites). This step runs automatically — students never need to trigger it.

### Math Notation
The math format (MathJax `$...$` / `$$...$$` or LaTeX `\(...\)` / `\[...\]`) is respected across all three passes including the refine step, so notation is consistent throughout the output.

### Copy to Obsidian
The **Copy Text** button always normalises math notation to Obsidian-compatible `$...$` / `$$...$$` regardless of which format was used during generation, so notes paste correctly into Obsidian every time.

> **Typical generation time:** 30–50 seconds for a full lecture (3 API calls in sequence). The progress bar shows which pass is running.

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

### Required

1. **C++ Compiler** — MSVC (Visual Studio 2019+), GCC, or Clang with C++17 support
2. **CMake** ≥ 3.14 — [Download](https://cmake.org/download/)
3. **yt-dlp** — `pip install yt-dlp` or [GitHub releases](https://github.com/yt-dlp/yt-dlp/releases)
4. **FFmpeg** — [Download](https://ffmpeg.org/download.html) *(all media processing)*

### Optional

| Dependency      | Used for                          | Install                                                                 |
|-----------------|-----------------------------------|-------------------------------------------------------------------------|
| **rembg**       | AI background removal             | `pip install rembg`                                                     |
| **Ghostscript** | PDF compress / merge / split      | [ghostscript.com](https://www.ghostscript.com/releases/gsdnld.html)    |
| **Pandoc**      | Markdown to PDF                   | [pandoc.org](https://pandoc.org/installing.html)                        |
| **ImageMagick** | SVG rasterisation (image-convert) | [imagemagick.org](https://imagemagick.org/script/download.php#windows) |
| **Groq API key**| All AI tools                      | [console.groq.com](https://console.groq.com) — set `GROQ_API_KEY`      |

> **SVG conversion note:** Image Convert uses Canvas in the browser for regular images. SVG files are sent to the server and rasterised via ImageMagick (`magick`), Inkscape, or `rsvg-convert` — whichever is found first. Without one of these installed, SVG conversion will fail with a clear error message.

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
sudo apt install ffmpeg cmake g++ ghostscript pandoc   # Ubuntu/Debian
brew install ffmpeg cmake ghostscript pandoc           # macOS

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

Set the `STATS_PASSWORD` and `DISCORD_WEBHOOK_URL` environment variables before starting the server. The dashboard and Discord logs are **disabled** if these variables are not set.

**Local / dev:**
```powershell
$env:STATS_PASSWORD      = "your-password"
$env:DISCORD_WEBHOOK_URL = "your-webhook-url"
$env:GROQ_API_KEY        = "your-groq-key"   # optional, enables AI tools
.\run.bat
```

**NSSM (Windows Service):**
1. Open NSSM: `nssm edit LumaTools`
2. Go to the **Environment** tab
3. Add each variable on its own line: `STATS_PASSWORD=...`, `DISCORD_WEBHOOK_URL=...`, `GROQ_API_KEY=...`
4. Click *Edit service* then restart: `nssm restart LumaTools`

**Linux systemd:**
```ini
# /etc/systemd/system/luma-tools.service
[Service]
Environment="STATS_PASSWORD=your-password"
Environment="DISCORD_WEBHOOK_URL=your-webhook-url"
Environment="GROQ_API_KEY=your-groq-key"
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
│   ├── routes_tools.cpp       # File processing tool endpoints (40+ tools)
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
│       ├── wasm.js            # Canvas API (image tools) + ffmpeg.wasm (audio)
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

| Method | Endpoint              | Description                       |
|--------|-----------------------|-----------------------------------|
| POST   | `/api/detect`         | Detect platform from URL          |
| POST   | `/api/analyze`        | Get media info (title, formats)   |
| POST   | `/api/download`       | Start a download                  |
| GET    | `/api/resolve-title`  | Resolve title from URL            |
| GET    | `/api/status/:id`     | Check download progress           |
| GET    | `/api/health`         | Server health, versions, git info |

### File Processing Tools

| Method | Endpoint                           | Description                              |
|--------|------------------------------------|------------------------------------------|
| POST   | `/api/tools/image-compress`        | Compress an image                        |
| POST   | `/api/tools/image-resize`          | Resize an image                          |
| POST   | `/api/tools/image-convert`         | Convert image format (SVG rasterised)    |
| POST   | `/api/tools/image-crop`            | Crop an image                            |
| POST   | `/api/tools/image-watermark`       | Add text watermark to an image           |
| POST   | `/api/tools/image-bg-remove`       | Remove image background                  |
| POST   | `/api/tools/metadata-strip`        | Strip image metadata                     |
| POST   | `/api/tools/audio-convert`         | Convert audio format                     |
| POST   | `/api/tools/audio-trim`            | Trim audio by time range (async)         |
| POST   | `/api/tools/audio-normalize`       | Normalise audio loudness                 |
| POST   | `/api/tools/video-compress`        | Compress video (async)                   |
| POST   | `/api/tools/video-trim`            | Trim video (async)                       |
| POST   | `/api/tools/video-convert`         | Convert video (async)                    |
| POST   | `/api/tools/video-extract-audio`   | Extract audio track (async)              |
| POST   | `/api/tools/video-to-gif`          | Convert video to GIF (async)             |
| POST   | `/api/tools/gif-to-video`          | Convert GIF to video (async)             |
| POST   | `/api/tools/video-remove-audio`    | Remove audio track (async)               |
| POST   | `/api/tools/video-speed`           | Change video speed (async)               |
| POST   | `/api/tools/video-frame`           | Extract video frame (async)              |
| POST   | `/api/tools/video-stabilize`       | Stabilize video (async)                  |
| POST   | `/api/tools/subtitle-extract`      | Extract subtitles (async)                |
| POST   | `/api/tools/redact-video`          | Blur/black-out regions in video          |
| POST   | `/api/tools/pdf-compress`          | Compress a PDF                           |
| POST   | `/api/tools/pdf-merge`             | Merge multiple PDFs                      |
| POST   | `/api/tools/pdf-split`             | Extract page range from a PDF            |
| POST   | `/api/tools/pdf-to-images`         | Convert PDF to images                    |
| POST   | `/api/tools/images-to-pdf`         | Convert images to PDF                    |
| POST   | `/api/tools/markdown-to-pdf`       | Convert Markdown to PDF (Pandoc)         |
| POST   | `/api/tools/csv-json`              | Convert CSV ↔ JSON                       |
| POST   | `/api/tools/favicon-generate`      | Generate favicon set                     |
| POST   | `/api/tools/hash-generate`         | Generate file hash                       |
| POST   | `/api/tools/citation-generate`     | Generate citation from URL or DOI        |
| POST   | `/api/tools/ai-study-notes`        | Generate study notes (Groq)              |
| POST   | `/api/tools/ai-improve-notes`      | Improve existing notes (Groq)            |
| POST   | `/api/tools/ai-flashcards`         | Generate flashcards (Groq)               |
| POST   | `/api/tools/ai-quiz`               | Generate quiz questions (Groq)           |
| POST   | `/api/tools/ai-paraphrase`         | Paraphrase text (Groq)                   |
| POST   | `/api/tools/ai-coverage-analysis`  | Analyse note coverage (Groq)             |
| POST   | `/api/mind-map`                    | Generate mind-map structure (Groq)       |
| POST   | `/api/youtube-summary`             | Summarise a YouTube video (Groq)         |
| GET    | `/api/tools/status/:id`            | Check async job status                   |
| GET    | `/api/tools/result/:id`            | Download processed file                  |
| GET    | `/api/tools/raw-text/:id`          | Retrieve plain-text job output           |
| GET    | `/api/tools/progress/:id`          | SSE stream of job progress               |

---

## Environment Variables

| Variable              | Default  | Description                                                              |
|-----------------------|----------|--------------------------------------------------------------------------|
| `PORT`                | `8080`   | Server listen port                                                       |
| `STATS_PASSWORD`      | *(none)* | Password for `/stats` dashboard. **Disabled if not set.**                |
| `DISCORD_WEBHOOK_URL` | *(none)* | Discord webhook for tool/download logs and daily digest.                 |
| `GROQ_API_KEY`        | *(none)* | Groq API key. **All AI tools are disabled if not set.**                  |

---

## License

[Business Source License 1.1](LICENSE) — personal, non-commercial use only.
Converts to AGPL v3 on **February 18, 2031**.

