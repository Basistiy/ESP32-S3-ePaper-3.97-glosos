#!/usr/bin/env python3
"""
record_from_esp32.py
Streams audio from ESP32-S3 microphone over USB when a button is pressed on the device,
saves it to a standard WAV audio file on Mac, and immediately plays it via macOS `afplay`.
"""

import sys
import os
import site

# Ensure user site-packages are in sys.path if not present
if hasattr(site, "USER_SITE") and os.path.exists(site.USER_SITE) and site.USER_SITE not in sys.path:
    sys.path.insert(0, site.USER_SITE)

import glob
import time
import wave
import struct
import select
import argparse
import datetime
import subprocess

try:
    import serial
except ImportError:
    # Check common user site locations on macOS
    user_py = os.path.expanduser("~/Library/Python/3.9/lib/python/site-packages")
    if os.path.exists(user_py) and user_py not in sys.path:
        sys.path.insert(0, user_py)
    try:
        import serial
    except ImportError:
        print("Error: 'pyserial' is not installed. Please run: pip install pyserial", file=sys.stderr)
        sys.exit(1)

def find_serial_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if ports:
        return ports[0]
    # Fallback to any USB serial port if usbmodem isn't matching
    ports = sorted(glob.glob("/dev/cu.usb*"))
    if ports:
        return ports[0]
    raise RuntimeError("No ESP32 USB serial port (/dev/cu.usbmodem*) found! Make sure the board is connected.")

def receive_audio_stream(s, output_file, sample_rate=24000, channels=1, verbose=True):
    """
    Receives framed PCM packets from ESP32:
    Format: [0xAA 0x55] [uint16_t sample_count] [sample_count * int16_t (PCM)]
    End-of-stream: [0xAA 0x55 0x00 0x00]
    """
    wav_out = wave.open(output_file, "wb")
    wav_out.setnchannels(channels)
    wav_out.setsampwidth(2) # 16-bit
    wav_out.setframerate(sample_rate)

    total_samples = 0
    t_start = time.time()
    last_rx_time = time.time()

    if verbose:
        print(f"\n  🔴 [RECORDING IN PROGRESS] Streaming from ESP32 mic ({sample_rate}Hz, 16-bit mono)...")

    # Packet parser state
    while True:
        # Check timeout: if no packets for > 3.0s, consider recording ended
        if time.time() - last_rx_time > 3.5:
            if verbose:
                print("\n  ⚠️  Stream timeout: no data received for 3.5s.")
            break

        if s.in_waiting >= 4:
            b1 = s.read(1)
            if b1 != b"\xAA":
                continue
            b2 = s.read(1)
            if b2 != b"\x55":
                continue

            len_bytes = s.read(2)
            if len(len_bytes) < 2:
                break
            sample_count = struct.unpack("<H", len_bytes)[0]

            # Sample count 0 signals clean End-of-Stream
            if sample_count == 0:
                break

            bytes_to_read = sample_count * 2
            pcm_data = bytearray()
            read_start = time.time()
            while len(pcm_data) < bytes_to_read and (time.time() - read_start < 1.0):
                chunk = s.read(bytes_to_read - len(pcm_data))
                if chunk:
                    pcm_data.extend(chunk)
                else:
                    time.sleep(0.001)

            if len(pcm_data) == bytes_to_read:
                wav_out.writeframes(pcm_data)
                total_samples += sample_count
                last_rx_time = time.time()

                duration_sec = total_samples / float(sample_rate)
                if verbose:
                    sys.stdout.write(f"\r  🎙️  Recording: {duration_sec:.2f}s ({total_samples:,} samples) ")
                    sys.stdout.flush()
        else:
            time.sleep(0.005)

    wav_out.close()
    duration_sec = total_samples / float(sample_rate)
    if verbose:
        print(f"\n  ✅ Saved: {output_file} ({duration_sec:.2f}s, {total_samples:,} samples)")
    return total_samples

def play_audio(audio_file):
    """Plays an audio file on macOS using the built-in afplay utility."""
    print(f"  🔊 Playing {os.path.basename(audio_file)} on Mac speaker (afplay)...")
    try:
        subprocess.run(["afplay", audio_file], check=True)
        print("  ✨ Playback complete.\n")
    except Exception as e:
        print(f"  ⚠️  Playback failed: {e}\n")

def main():
    parser = argparse.ArgumentParser(description="Stream audio from ESP32 button press over USB and play on Mac.")
    parser.add_argument("-p", "--port", help="Serial port (default: auto-detected /dev/cu.usbmodem*)")
    parser.add_argument("-o", "--output", help="Output WAV file name (default: recording_TIMESTAMP.wav)")
    parser.add_argument("--once", action="store_true", help="Exit after a single recording")
    parser.add_argument("--no-play", action="store_true", help="Do not play audio after recording")
    parser.add_argument("--trigger", action="store_true", help="Trigger recording immediately from Mac on startup")
    parser.add_argument("--play-on-device", action="store_true", help="Tell ESP32 to replay recording on its onboard speaker too")
    args = parser.parse_args()

    port = args.port or find_serial_port()
    print("=" * 64)
    print("  ESP32-S3 Audio Stream Receiver & Mac Player")
    print("=" * 64)
    print(f"Connecting to ESP32 on: {port} ...")

    s = serial.Serial(port, 115200, timeout=0.1)
    s.dtr = True
    s.rts = True
    time.sleep(0.3)
    s.reset_input_buffer()

    print("\n🎧 Listener active! Ready for audio events:")
    print("  • Press BOOT button (GPIO 0) or Rotary Wheel (GPIO 5) on device to record")
    print("  • Or press [Enter] in this terminal to trigger recording from Mac ('R')")
    print("  • Type '4' + [Enter] to test onboard speaker replay on device")
    print("  • Press Ctrl+C to exit\n" + "-" * 64)

    if args.trigger:
        print("Sending recording trigger ('R') to ESP32...")
        s.write(b"R\n")
        s.flush()

    try:
        while True:
            # Check keyboard input from user (non-blocking)
            if select.select([sys.stdin], [], [], 0.0)[0]:
                user_cmd = sys.stdin.readline().strip()
                if user_cmd.lower() in ("q", "quit", "exit"):
                    print("Exiting...")
                    break
                elif user_cmd == "4":
                    print("Requesting ESP32 to replay last recording on onboard speaker ('4')...")
                    s.write(b"4\n")
                    s.flush()
                else:
                    print("Triggering recording on ESP32 ('R')...")
                    s.write(b"R\n")
                    s.flush()

            # Check serial output from ESP32
            if s.in_waiting:
                # Check for sync header directly in case text was skipped
                header_peek = s.read(min(s.in_waiting, 64))
                if b"RECORD_START" in header_peek or b"\xAA\x55" in header_peek:
                    # Determine output file path
                    if args.output:
                        out_path = args.output
                    else:
                        ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
                        out_path = f"recording_{ts}.wav"

                    total_samples = receive_audio_stream(s, out_path, sample_rate=24000, channels=1)

                    if total_samples > 0:
                        if not args.no_play:
                            play_audio(out_path)

                        if args.play_on_device:
                            print("Requesting replay on device speaker...")
                            s.write(b"4\n")
                            s.flush()

                    if args.once:
                        break

                    print("-" * 64)
                    print("🎧 Ready for next recording! Press button on device or [Enter] here:")
                else:
                    # Print normal debug/log lines from ESP32
                    try:
                        text = header_peek.decode("utf-8", errors="ignore")
                        for line in text.splitlines():
                            line_s = line.strip()
                            if line_s:
                                print(f"  [ESP32] {line_s}")
                    except Exception:
                        pass
            else:
                time.sleep(0.01)

    except KeyboardInterrupt:
        print("\nStopping listener...")
    finally:
        s.close()
        print("Serial port closed. Goodbye!")

if __name__ == "__main__":
    main()
