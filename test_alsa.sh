#!/usr/bin/env bash
# ==============================================================================
# ESP32-S3 e-Paper 3.97 ALSA Audio Device Test Script
# Tests native USB Audio Class (UAC) capture (mic) and playback (speaker) on Linux
# ==============================================================================

set -e

echo "=== 1. Checking USB Device ==="
if command -v lsusb &> /dev/null; then
    lsusb | grep -i -E "audio|waveshare|cafe|espressif" || lsusb
else
    echo "lsusb not found, skipping."
fi

echo ""
echo "=== 2. Listing ALSA Playback Devices (aplay -l) ==="
aplay -l

echo ""
echo "=== 3. Listing ALSA Capture Devices (arecord -l) ==="
arecord -l

# Auto-detect ALSA card number for the ESP32 audio device
CARD_NUM=$(arecord -l 2>/dev/null | grep -i -E "audio|waveshare|uac" | head -n1 | sed -E 's/card ([0-9]+):.*/\1/' || true)

if [ -z "$CARD_NUM" ]; then
    echo ""
    echo "⚠️  Could not auto-detect card number. Please check the lists above and specify:"
    echo "   Example: $0 <card_number>"
    if [ -n "$1" ]; then
        CARD_NUM="$1"
    else
        exit 0
    fi
fi

echo ""
echo "🎯 Using ALSA Card #$CARD_NUM"
DEVICE="plughw:$CARD_NUM,0"

echo ""
echo "=== 4. Testing Microphone Recording (5 seconds at 48kHz 16-bit) ==="
echo "🎙️  Recording from ESP32 microphone... Speak into the onboard mic!"
arecord -D "$DEVICE" -f S16_LE -r 48000 -c 2 -d 5 test_esp32_mic.wav
echo "✅ Recording saved to test_esp32_mic.wav"

echo ""
echo "=== 5. Testing Speaker Playback ==="
echo "🔊 Playing recorded audio back through ESP32 onboard speaker..."
aplay -D "$DEVICE" test_esp32_mic.wav
echo "✅ Playback finished!"

echo ""
echo "🎉 ALSA Audio Test Complete!"
echo "You can also adjust volume/mute via: alsamixer -c $CARD_NUM"
