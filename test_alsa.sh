#!/usr/bin/env bash
# ==============================================================================
# test_alsa.sh - Test ESP32-S3 USB Audio Class (UAC) on Linux (ALSA)
# ==============================================================================
# Usage:
#   ./test_alsa.sh [record|play|loopback|info]
# ==============================================================================

set -euo pipefail

CARD_NAME="Device"
DEV_INDEX=0
SAMPLE_RATE=16000
CHANNELS=1
FORMAT="S16_LE"
TEST_FILE="test_esp32_audio.wav"

# Check if card is present
find_card() {
    if aplay -l 2>/dev/null | grep -q "ESP UAC Device"; then
        echo "Found 'ESP UAC Device' in ALSA playback devices."
    elif arecord -l 2>/dev/null | grep -q "ESP UAC Device"; then
        echo "Found 'ESP UAC Device' in ALSA capture devices."
    else
        echo "WARNING: 'ESP UAC Device' not found in aplay -l or arecord -l."
        echo "Available playback devices:"
        aplay -l 2>/dev/null || true
        echo "Available capture devices:"
        arecord -l 2>/dev/null || true
    fi
}

ALSA_DEVICE="plughw:CARD=${CARD_NAME},DEV=${DEV_INDEX}"

cmd_info() {
    echo "=== ALSA Devices ==="
    find_card
    echo ""
    echo "=== ALSA Mixer Controls (amixer -c ${CARD_NAME}) ==="
    amixer -c "${CARD_NAME}" 2>/dev/null || echo "(No hardware mixer or card not found)"
}

cmd_record() {
    local duration="${1:-5}"
    echo "Recording ${duration}s from ESP32 microphone (${ALSA_DEVICE}) at ${SAMPLE_RATE}Hz..."
    arecord -D "${ALSA_DEVICE}" -f "${FORMAT}" -r "${SAMPLE_RATE}" -c "${CHANNELS}" -d "${duration}" "${TEST_FILE}"
    echo "Recording complete: ${TEST_FILE} ($(du -h "${TEST_FILE}" | cut -f1))"
}

cmd_play() {
    local file="${1:-${TEST_FILE}}"
    if [ ! -f "${file}" ]; then
        echo "Error: File '${file}' not found. Record first or specify an existing WAV file."
        exit 1
    fi
    echo "Playing '${file}' to ESP32 speaker (${ALSA_DEVICE})..."
    aplay -D "${ALSA_DEVICE}" "${file}"
    echo "Playback complete."
}

cmd_tone() {
    echo "Generating and playing a 1-second 1kHz test beep to ESP32 speaker..."
    if command -v speaker-test >/dev/null 2>&1; then
        speaker-test -D "${ALSA_DEVICE}" -t sine -f 1000 -l 1 -r "${SAMPLE_RATE}" -c 1
    elif command -v python3 >/dev/null 2>&1; then
        python3 -c "
import wave, struct, math
sample_rate = 16000
duration = 1.5
freq = 880.0
n_samples = int(sample_rate * duration)
with wave.open('/tmp/test_tone.wav', 'wb') as wav:
    wav.setnchannels(1)
    wav.setsampwidth(2)
    wav.setframerate(sample_rate)
    for i in range(n_samples):
        val = int(16000 * math.sin(2.0 * math.pi * freq * (i / sample_rate)))
        wav.writeframes(struct.pack('<h', val))
"
        aplay -D "${ALSA_DEVICE}" /tmp/test_tone.wav
        rm -f /tmp/test_tone.wav
    else
        echo "Neither speaker-test nor python3 available to generate tone."
    fi
}

cmd_loopback() {
    local duration="${1:-4}"
    echo "=== Testing Microphone Record (${duration}s) ==="
    cmd_record "${duration}"
    echo ""
    echo "=== Testing Speaker Playback of Recorded Audio ==="
    cmd_play "${TEST_FILE}"
    echo "=== Test Successful! ==="
}

MODE="${1:-loopback}"
case "${MODE}" in
    info)
        cmd_info
        ;;
    record)
        cmd_record "${2:-5}"
        ;;
    play)
        cmd_play "${2:-${TEST_FILE}}"
        ;;
    tone)
        cmd_tone
        ;;
    loopback)
        cmd_loopback "${2:-4}"
        ;;
    *)
        echo "Usage: $0 [info | record [sec] | play [file] | tone | loopback [sec]]"
        exit 1
        ;;
esac
