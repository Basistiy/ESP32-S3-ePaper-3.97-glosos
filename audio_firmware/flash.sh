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

PORT=$(ls -1 /dev/cu.usbmodem* 2>/dev/null | head -n 1 || true)
if [ -z "$PORT" ]; then
    echo "⚠️  No /dev/cu.usbmodem* port detected."
    echo "   Please put the board in Download Mode:"
    echo "   1. Hold the BOOT button (GPIO 0)"
    echo "   2. Tap the RESET button"
    echo "   3. Release the BOOT button"
    exit 1
fi

echo "🚀 Flashing audio_firmware to $PORT..."
idf.py -p "$PORT" flash
echo "🎉 Flashing finished!"
