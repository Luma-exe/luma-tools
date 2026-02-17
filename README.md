# Luma Tools — Universal Media Downloader

A sleek, modern media downloader with a **C++ backend** and beautiful glassmorphism UI.  
Paste any URL and it auto-detects the platform, giving you the right download options.

![C++](https://img.shields.io/badge/Backend-C%2B%2B17-blue?logo=cplusplus)
![yt-dlp](https://img.shields.io/badge/Engine-yt--dlp-red)

---

## Supported Platforms

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

---

## Prerequisites

1. **C++ Compiler** — MSVC (Visual Studio), GCC, or Clang with C++17 support
2. **CMake** ≥ 3.14 — [Download](https://cmake.org/download/)
3. **yt-dlp** — `pip install yt-dlp` or [GitHub releases](https://github.com/yt-dlp/yt-dlp/releases)
4. **ffmpeg** (recommended) — [Download](https://ffmpeg.org/download.html)  
   *Required for audio extraction and format conversion*

---

## Quick Start

### Windows

```powershell
# 1. Install dependencies
pip install yt-dlp
# Install ffmpeg and add to PATH

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

## Architecture

```
luma-tools/
├── src/
│   └── main.cpp          # C++ HTTP server (cpp-httplib + nlohmann/json)
├── public/
│   ├── index.html         # Main page
│   ├── styles.css         # Glassmorphism UI styles
│   └── app.js             # Frontend logic
├── CMakeLists.txt         # Build configuration (auto-fetches dependencies)
├── build.bat / build.sh   # Build scripts
└── run.bat / run.sh       # Run scripts
```

**Backend Stack:**
- **cpp-httplib** — Lightweight header-only HTTP server
- **nlohmann/json** — JSON parsing/serialization
- **yt-dlp** — Media extraction engine (invoked via subprocess)

**Frontend:**
- Vanilla HTML/CSS/JS (no frameworks)
- Animated gradient background with particle effects
- Glassmorphism design with smooth transitions
- Auto-detects platform from pasted URLs

---

## API Endpoints

| Method | Endpoint             | Description                    |
|--------|----------------------|--------------------------------|
| POST   | `/api/detect`        | Detect platform from URL       |
| POST   | `/api/analyze`       | Get media info (title, formats)|
| POST   | `/api/download`      | Start a download               |
| GET    | `/api/status/:id`    | Check download progress        |
| GET    | `/api/health`        | Server health + yt-dlp version |

---

## Environment Variables

| Variable | Default | Description       |
|----------|---------|-------------------|
| `PORT`   | `8080`  | Server port       |

---

## License

MIT
