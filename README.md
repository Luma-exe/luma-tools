
# Luma Tools — Universal Media Toolkit

A powerful, modern media toolkit with a C++17 backend and a beautiful glassmorphism UI.  
Download from 10+ platforms and process files with 28+ built-in tools — all from your browser.

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

| Tool                   | Description                                      |
|------------------------|--------------------------------------------------|
| Image Compress         | Reduce image file size (JPG, PNG, WebP)          |
| Image Resize           | Scale images by width/height                     |
| Image Convert          | Convert between PNG, JPG, WebP, BMP, TIFF        |
| Image Crop             | Crop images with aspect ratio presets            |
| Image Background Remover | Remove backgrounds (AI/white/black)           |
| Audio Convert          | Convert between MP3, AAC, WAV, FLAC, OGG, WMA    |
| Video Compress         | Reduce video size (light/medium/heavy presets)   |
| Video Trim             | Cut video segments (fast or frame-precise mode)  |
| Video Convert          | Convert between MP4, WebM, MKV, AVI, MOV, GIF    |
| Extract Audio          | Rip audio track from any video file               |
| PDF Compress           | Shrink PDF files via Ghostscript                  |
| PDF Merge              | Combine multiple PDFs into one                    |
| PDF to Images          | Convert PDF pages to PNG, JPG, or TIFF            |
| Favicon Generator      | Create favicons from images                       |
| Hash Generator         | Compute file hashes (MD5, SHA1, SHA256, etc.)     |
| Base64 Encode/Decode   | Encode or decode files/text as Base64             |
| Color Converter        | Convert between HEX, RGB, HSL, etc.               |
| Markdown Preview       | Live markdown editor with HTML export             |
| ...and many more!      | (See the app for the full list)                   |

---

## UI/UX Highlights

- **Sidebar search** (with `Ctrl+K` shortcut) to instantly filter tools
- **Recent tools** section (remembers your last 5 tools)
- **Dark/light theme toggle** (persistent)
- **Smart drag & drop** — drop files anywhere, auto-routes to the right tool
- **Split-pane Markdown Preview** with live HTML export
- **Glassmorphism dark/light theme** with smooth transitions
- **Animated background particles**
- **Responsive design** — works on desktop and mobile

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

## Architecture

```
luma-tools/
├── src/
│   ├── headers/
│   │   ├── common.h           # Shared includes, globals, utility declarations
│   │   ├── discord.h          # Discord webhook function declarations
│   │   └── routes.h           # Route registration declarations
│   ├── main.cpp               # Server init, executable discovery, startup
│   ├── common.cpp             # Utility functions, download/job managers
│   ├── platform.cpp           # Platform detection (YouTube, TikTok, etc.)
│   ├── discord.cpp            # Discord webhook logging (rich embeds)
│   ├── routes_download.cpp    # Download API endpoints
│   └── routes_tools.cpp       # File processing tool endpoints
├── public/
│   ├── index.html             # Main page (sidebar + tool panels)
│   ├── styles.css             # Glassmorphism dark/light theme
│   └── app.js                 # Frontend logic
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

| Method | Endpoint                       | Description              |
|--------|--------------------------------|--------------------------|
| POST   | `/api/tools/image-compress`    | Compress an image        |
| POST   | `/api/tools/image-resize`      | Resize an image          |
| POST   | `/api/tools/image-convert`     | Convert image format     |
| POST   | `/api/tools/image-crop`        | Crop an image            |
| POST   | `/api/tools/image-bg-remove`   | Remove image background  |
| POST   | `/api/tools/audio-convert`     | Convert audio format     |
| POST   | `/api/tools/video-compress`    | Compress video (async)   |
| POST   | `/api/tools/video-trim`        | Trim video (async)       |
| POST   | `/api/tools/video-convert`     | Convert video (async)    |
| POST   | `/api/tools/video-extract-audio` | Extract audio (async) |
| POST   | `/api/tools/pdf-compress`      | Compress a PDF           |
| POST   | `/api/tools/pdf-merge`         | Merge multiple PDFs      |
| POST   | `/api/tools/pdf-to-images`     | Convert PDF to images    |
| POST   | `/api/tools/favicon-generate`  | Generate favicon         |
| POST   | `/api/tools/hash-generate`     | Generate file hash       |
| GET    | `/api/tools/status/:id`        | Check async job progress |
| GET    | `/api/tools/result/:id`        | Download processed file  |

---

## Environment Variables

| Variable | Default | Description       |
|----------|---------|-------------------|
| `PORT`   | `8080`  | Server port       |

---

## License

MIT

---

## Quick Start

### Windows

```powershell
# 1. Install dependencies
pip install yt-dlp
winget install Gyan.FFmpeg

# 2. Build
.\build.bat

# 3. Run
.\run.bat
```

### Linux / macOS

```bash
# 1. Install dependencies
pip3 install yt-dlp
sudo apt install ffmpeg cmake g++   # Ubuntu/Debian
brew install ffmpeg cmake            # macOS

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
- Installing CMake, FFmpeg, yt-dlp, and Ghostscript
- Building the project
- Setting up Caddy as a reverse proxy with automatic HTTPS
- Registering Windows services via NSSM (LumaTools + LumaToolsCaddy)

Additional deploy helpers:
- `deploy/restart.bat` — Restart services
- `deploy/status.bat` — Check service status

---

## Architecture

```
luma-tools/
├── src/
│   ├── headers/
│   │   ├── common.h           # Shared includes, globals, utility declarations
│   │   ├── discord.h          # Discord webhook function declarations
│   │   └── routes.h           # Route registration declarations
│   ├── main.cpp               # Server init, executable discovery, startup
│   ├── common.cpp             # Utility functions, download/job managers
│   ├── platform.cpp           # Platform detection (YouTube, TikTok, etc.)
│   ├── discord.cpp            # Discord webhook logging (rich embeds)
│   ├── routes_download.cpp    # Download API endpoints
│   └── routes_tools.cpp       # File processing tool endpoints
├── public/
│   ├── index.html             # Main page (sidebar + tool panels)
│   ├── styles.css             # Glassmorphism dark theme
│   └── app.js                 # Frontend logic
├── deploy/
│   ├── deploy-windows.ps1     # Full VPS deployment script
│   ├── restart.bat            # Service restart helper
│   └── status.bat             # Service status helper
├── CMakeLists.txt             # Build config (auto-fetches dependencies)
├── build.bat / build.sh       # Build scripts
└── run.bat / run.sh           # Run scripts
```

### Backend Stack

- **C++17** — Modern C++ with `std::filesystem`, threads, etc.
- **cpp-httplib v0.15.3** — Lightweight header-only HTTP server
- **nlohmann/json v3.11.3** — JSON parsing/serialization
- **yt-dlp** — Media extraction engine (invoked via subprocess)
- **FFmpeg** — All media processing (compress, convert, trim, extract)
- **Ghostscript** — PDF processing (compress, merge, rasterize)
- **Discord Webhooks** — Activity logging with device hostname identification

### Frontend

- Vanilla HTML/CSS/JS (no frameworks)
- Animated gradient background with particle effects
- Glassmorphism design with smooth transitions
- Auto-detects platform from pasted URLs
- Scrolling status ticker showing service health and git version

---

## API Endpoints

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

| Method | Endpoint                       | Description              |
|--------|--------------------------------|--------------------------|
| POST   | `/api/tools/image-compress`    | Compress an image        |
| POST   | `/api/tools/image-resize`      | Resize an image          |
| POST   | `/api/tools/image-convert`     | Convert image format     |
| POST   | `/api/tools/audio-convert`     | Convert audio format     |
| POST   | `/api/tools/video-compress`    | Compress video (async)   |
| POST   | `/api/tools/video-trim`        | Trim video (async)       |
| POST   | `/api/tools/video-convert`     | Convert video (async)    |
| POST   | `/api/tools/video-extract-audio` | Extract audio (async) |
| POST   | `/api/tools/pdf-compress`      | Compress a PDF           |
| POST   | `/api/tools/pdf-merge`         | Merge multiple PDFs      |
| POST   | `/api/tools/pdf-to-images`     | Convert PDF to images    |
| GET    | `/api/tools/status/:id`        | Check async job progress |
| GET    | `/api/tools/result/:id`        | Download processed file  |

---

## Environment Variables

| Variable | Default | Description       |
|----------|---------|-------------------|
| `PORT`   | `8080`  | Server port       |

---

## License

MIT
