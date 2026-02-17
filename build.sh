#!/bin/bash
echo "============================================"
echo "  LUMA TOOLS - Build Script (Linux/macOS)"
echo "============================================"
echo

# Check dependencies
command -v cmake >/dev/null 2>&1 || { echo "[ERROR] CMake not found! Install: sudo apt install cmake"; exit 1; }
command -v g++ >/dev/null 2>&1 || command -v clang++ >/dev/null 2>&1 || { echo "[ERROR] C++ compiler not found!"; exit 1; }

command -v yt-dlp >/dev/null 2>&1 || {
    echo "[WARNING] yt-dlp not found. Installing via pip..."
    pip3 install yt-dlp || echo "[ERROR] Failed. Install manually: pip3 install yt-dlp"
}

command -v ffmpeg >/dev/null 2>&1 || echo "[WARNING] ffmpeg not found. Some conversions may fail."

# Build
mkdir -p build && cd build
echo
echo "[1/2] Configuring..."
cmake .. -DCMAKE_BUILD_TYPE=Release
echo
echo "[2/2] Building..."
cmake --build . --config Release -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo
echo "============================================"
echo "  Build complete! Run: ./build/luma-tools"
echo "============================================"
