# ============================================================================
# luma-tools — Dockerfile
# Multi-stage build:
#   Stage 1 (builder) — Compiles the C++ binary using CMake. CMake's
#     FetchContent pulls cpp-httplib, nlohmann/json, and SQLite3 at configure
#     time, so no pre-installed system libraries are needed.
#   Stage 2 (runtime) — Minimal Ubuntu image with all external tool
#     dependencies (ffmpeg, yt-dlp, ghostscript, pandoc, ImageMagick, 7zip,
#     rembg) plus the compiled binary and static web frontend.
# ============================================================================

# ──────────────────────────────────────────────────────────────────────────
# Stage 1 — Build C++ binary
# ──────────────────────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN sed -i 's|http://archive.ubuntu.com/ubuntu|https://archive.ubuntu.com/ubuntu|g; s|http://security.ubuntu.com/ubuntu|https://security.ubuntu.com/ubuntu|g' /etc/apt/sources.list.d/ubuntu.sources
RUN printf 'Acquire::Retries "10";\nAcquire::Queue-Mode "access";\nAcquire::http::Timeout "30";\nAcquire::https::Timeout "30";\nAcquire::http::Pipeline-Depth "0";\nAcquire::https::Pipeline-Depth "0";\nAcquire::https::Verify-Peer "false";\nAcquire::https::Verify-Host "false";\n' > /etc/apt/apt.conf.d/80-retries

RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake \
        make \
        g++ \
        git \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy build files (CMakeLists.txt fetches deps on first configure)
COPY CMakeLists.txt ./
COPY src/           ./src/
COPY public/        ./public/

# Configure and build — FetchContent downloads deps at this step
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --config Release -j$(nproc)

# ──────────────────────────────────────────────────────────────────────────
# Stage 2 — Production runtime
# ──────────────────────────────────────────────────────────────────────────
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN sed -i 's|http://archive.ubuntu.com/ubuntu|https://archive.ubuntu.com/ubuntu|g; s|http://security.ubuntu.com/ubuntu|https://security.ubuntu.com/ubuntu|g' /etc/apt/sources.list.d/ubuntu.sources
RUN printf 'Acquire::Retries "10";\nAcquire::Queue-Mode "access";\nAcquire::http::Timeout "30";\nAcquire::https::Timeout "30";\nAcquire::http::Pipeline-Depth "0";\nAcquire::https::Pipeline-Depth "0";\nAcquire::https::Verify-Peer "false";\nAcquire::https::Verify-Host "false";\n' > /etc/apt/apt.conf.d/80-retries

# Force this stage to wait for the builder before apt runs; BuildKit otherwise
# starts both stages in parallel and the VPS can choke on the combined apt load.
COPY --from=builder /app/build/luma-tools /tmp/.builder-ready

# Install all external tool dependencies that luma-tools discovers at startup
RUN apt-get update && apt-get install -y --no-install-recommends \
        # Media processing
        ffmpeg \
        ghostscript \
        pandoc \
        imagemagick \
        p7zip-full \
        # Python (yt-dlp, rembg, demucs)
        python3 \
        python3-pip \
        # weasyprint system deps (for markdown-to-pdf)
        libpango-1.0-0 \
        libpangoft2-1.0-0 \
        libpangocairo-1.0-0 \
        # OCR
        tesseract-ocr \
        tesseract-ocr-eng \
        tesseract-ocr-fra \
        tesseract-ocr-deu \
        tesseract-ocr-spa \
        tesseract-ocr-chi-sim \
        tesseract-ocr-chi-tra \
        tesseract-ocr-jpn \
        tesseract-ocr-ara \
        # HEIC/HEIF support
        libheif-examples \
        # LibreOffice headless — for pdf↔docx conversion. Adds ~500 MB to the
        # image; the only reliable way to do high-fidelity PDF→Word.
        libreoffice-core \
        libreoffice-writer \
        # Misc
        ca-certificates \
        curl \
        openssl \
    && rm -rf /var/lib/apt/lists/*

# Install Python-based tools globally (demucs last — large install)
RUN pip3 install --no-cache-dir --break-system-packages yt-dlp rembg weasyprint
RUN pip3 install --no-cache-dir --break-system-packages demucs

WORKDIR /app

# Copy compiled binary from builder
# CMake places the binary at build/luma-tools (Linux single-config generator)
COPY --from=builder /app/build/luma-tools ./luma-tools

# Copy web frontend (CMake copies public/ to build dir via POST_BUILD command)
COPY --from=builder /app/build/public ./public

# Pre-create writable runtime directories (mounted as volumes in compose)
RUN mkdir -p downloads processing

ENV PORT=8080

EXPOSE 8080

# Healthcheck: hits the cheap /healthz endpoint. After 3 consecutive failures
# Docker flags the container "unhealthy" — the autoheal sidecar (see
# docker-compose.yml) watches for that label and restarts the container.
# start-period gives the binary 30s to come up before failures start counting.
HEALTHCHECK --interval=30s --timeout=5s --start-period=30s --retries=3 \
    CMD curl -fsS --max-time 4 http://127.0.0.1:8080/healthz || exit 1

ENTRYPOINT ["./luma-tools"]
