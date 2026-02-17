#!/bin/bash
echo "Starting Luma Tools Server..."
cd "$(dirname "$0")"

if [ -f build/luma-tools ]; then
    cd build && ./luma-tools
elif [ -f build/Release/luma-tools ]; then
    cd build/Release && ./luma-tools
else
    echo "[ERROR] Binary not found! Run build.sh first."
    exit 1
fi
