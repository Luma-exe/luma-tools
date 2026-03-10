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

# Install all external tool dependencies that luma-tools discovers at startup
RUN apt-get update && apt-get install -y --no-install-recommends \
        # Media processing
        ffmpeg \
        ghostscript \
        pandoc \
        imagemagick \
        p7zip-full \
        # Python (yt-dlp and rembg)
        python3 \
        python3-pip \
        # Misc
        ca-certificates \
        curl \
    && rm -rf /var/lib/apt/lists/*

# Install Python-based tools globally
RUN pip3 install --no-cache-dir --break-system-packages yt-dlp rembg

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

ENTRYPOINT ["./luma-tools"]
