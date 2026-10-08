#!/usr/bin/env python3
import sys
import os
import glob
import time
import subprocess
import serial
import struct

def find_serial_port():
    ports = glob.glob("/dev/cu.usbmodem*")
    if ports:
        return sorted(ports)[0]
    raise RuntimeError("No ESP32 USB serial port (/dev/cu.usbmodem*) found!")

def main():
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python stream_to_esp32.py <audio_file_or_path>")
        print("  python stream_to_esp32.py --say \"Your text here\"")
        print("\nExamples:")
        print("  python stream_to_esp32.py /System/Library/Sounds/Glass.aiff")
        print("  python stream_to_esp32.py /System/Library/Sounds/Hero.aiff")
        print("  python stream_to_esp32.py --say \"Hello! Sound is streaming over USB!\"")
        sys.exit(1)

    temp_file = None
    if sys.argv[1] == "--say":
        text = " ".join(sys.argv[2:]) if len(sys.argv) > 2 else "Hello from your Mac!"
        temp_file = "/tmp/esp32_speech.aiff"
        print(f"Generating speech: \"{text}\"...")
        subprocess.run(["say", "-o", temp_file, text], check=True)
        audio_file = temp_file
    else:
        audio_file = sys.argv[1]
        if not os.path.exists(audio_file):
            print(f"Error: file not found: {audio_file}")
            sys.exit(1)

    port = find_serial_port()
    print(f"Connecting to ESP32 on {port}...")

    s = serial.Serial(port, 115200, timeout=2)
    s.dtr = True
    s.rts = True
    time.sleep(0.3)

    # Flush input buffer
    s.reset_input_buffer()

    print("Requesting stream mode from ESP32 ('S')...")
    s.write(b"S\n")
    s.flush()

    # Wait for STREAM_READY
    t0 = time.time()
    ready = False
    while time.time() - t0 < 3:
        line = s.readline().decode("utf-8", errors="replace").strip()
        if "STREAM_READY" in line:
            ready = True
            break

    if not ready:
        print("Warning: Did not see STREAM_READY, proceeding with stream anyway...")

    # Start ffmpeg process to convert to 24000Hz 16-bit mono PCM
    cmd = [
        "ffmpeg", "-y", "-v", "error",
        "-i", audio_file,
        "-f", "s16le",
        "-ac", "1",
        "-ar", "24000",
        "-"
    ]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    print(f"Streaming {audio_file} (24kHz 16-bit PCM) over USB with packet framing...")
    CHUNK_SAMPLES = 256  # 512 bytes PCM per packet
    CHUNK_BYTES = CHUNK_SAMPLES * 2
    bytes_streamed = 0
    t_audio_clock = 0.0
    t_start = time.time()

    while True:
        data = proc.stdout.read(CHUNK_BYTES)
        if not data:
            break
        if len(data) % 2 != 0:
            data += b"\x00"

        samples = len(data) // 2
        # Framed packet: [0xAA, 0x55] + [uint16_t sample_count] + [raw samples]
        packet = b"\xAA\x55" + struct.pack("<H", samples) + data
        s.write(packet)
        bytes_streamed += len(data)
        t_audio_clock += samples / 24000.0

        # Flow control: maintain ~100ms buffer lead
        ahead = t_audio_clock - (time.time() - t_start)
        if ahead > 0.15:
            time.sleep(ahead - 0.10)

    proc.wait()
    # Send clean End-of-Stream packet (sample_count = 0)
    s.write(b"\xAA\x55\x00\x00")
    s.flush()

    total_audio_sec = bytes_streamed / 48000.0
    print(f"Sent {bytes_streamed} bytes ({total_audio_sec:.2f}s audio). Waiting for playback to finish...")

    # Wait for ESP32 to finish playing and send STREAM_DONE
    t_wait = time.time()
    while time.time() - t_wait < 5.0:
        if s.in_waiting:
            line = s.readline().decode("utf-8", errors="replace").strip()
            if line:
                print(f"ESP32: {line}")
                if "STREAM_DONE" in line:
                    break
        else:
            time.sleep(0.05)

    s.close()
    if temp_file and os.path.exists(temp_file):
        try:
            os.remove(temp_file)
        except Exception:
            pass
    print("Done!")

if __name__ == "__main__":
    main()
