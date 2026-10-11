#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ -f "$HOME/esp/esp-idf/export.sh" ]; then
    . "$HOME/esp/esp-idf/export.sh"
else
    echo "ERROR: ESP-IDF export.sh not found at $HOME/esp/esp-idf/export.sh"
    exit 1
fi

if [ ! -d "build" ]; then
    idf.py set-target esp32s3
fi

idf.py build
echo ""
echo "✅ Build complete! Run ./flash.sh to upload to the board."
