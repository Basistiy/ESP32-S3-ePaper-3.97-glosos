#!/usr/bin/env python3
"""
send_chat.py
Send chat messages and control commands from Mac to the ESP32-S3 e-Paper display.
Supports:
  - User messages (rendered on the RIGHT side in black bubbles with white text)
  - Agent messages (rendered on the LEFT side in white bubbles with black border)
  - Clear screen, rotation toggling (portrait 90° / 270°), and interactive chat mode.
"""

import sys
import os
import site
import glob
import time
import argparse
import datetime

# Ensure user site packages (e.g. pyserial) are accessible
if hasattr(site, "USER_SITE") and os.path.exists(site.USER_SITE) and site.USER_SITE not in sys.path:
    sys.path.insert(0, site.USER_SITE)

user_py = os.path.expanduser("~/Library/Python/3.9/lib/python/site-packages")
if os.path.exists(user_py) and user_py not in sys.path:
    sys.path.insert(0, user_py)

try:
    import serial
except ImportError:
    print("Error: 'pyserial' is not installed. Please run: pip install pyserial", file=sys.stderr)
    sys.exit(1)


def find_serial_port(preferred_port=None):
    if preferred_port and os.path.exists(preferred_port):
        return preferred_port
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if ports:
        return ports[0]
    ports = sorted(glob.glob("/dev/cu.usb*"))
    if ports:
        return ports[0]
    raise RuntimeError("No ESP32 USB serial port (/dev/cu.usbmodem*) found! Check USB connection.")


class ESP32ChatClient:
    def __init__(self, port=None, baudrate=115200, timeout=2.0):
        self.port = find_serial_port(port)
        self.baudrate = baudrate
        self.timeout = timeout
        self.ser = None

    def connect(self):
        print(f"Connecting to ESP32 on {self.port} ({self.baudrate} baud)...")
        self.ser = serial.Serial(self.port, self.baudrate, timeout=self.timeout)
        self.ser.dtr = True
        self.ser.rts = True
        time.sleep(0.3)
        self.ser.reset_input_buffer()
        print("Connected successfully!")

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def send_raw(self, cmd_line, wait_response=True, wait_sec=4.0):
        if not self.ser or not self.ser.is_open:
            self.connect()

        if not cmd_line.endswith("\n"):
            cmd_line += "\n"

        self.ser.write(cmd_line.encode("utf-8"))
        self.ser.flush()

        responses = []
        if wait_response:
            t0 = time.time()
            while time.time() - t0 < wait_sec:
                if self.ser.in_waiting:
                    line = self.ser.readline().decode("utf-8", errors="ignore").strip()
                    if line:
                        responses.append(line)
                        if line == "OK" or "refresh complete" in line.lower():
                            break
                time.sleep(0.05)
        return responses

    def send_user_message(self, text, time_str=None):
        ts = time_str or datetime.datetime.now().strftime("%H:%M")
        json_cmd = f'{{"type":"user","text":"{text}","time":"{ts}"}}'
        print(f"\n[USER -> Device] (Right Bubble): \"{text}\" ({ts})")
        resp = self.send_raw(json_cmd)
        for r in resp:
            print(f"  [Device] {r}")

    def send_agent_message(self, text, time_str=None):
        ts = time_str or datetime.datetime.now().strftime("%H:%M")
        json_cmd = f'{{"type":"agent","text":"{text}","time":"{ts}"}}'
        print(f"\n[AGENT -> Device] (Left Bubble): \"{text}\" ({ts})")
        resp = self.send_raw(json_cmd)
        for r in resp:
            print(f"  [Device] {r}")

    def clear(self):
        print("\n[Command] Clearing chat screen...")
        resp = self.send_raw("/clear")
        for r in resp:
            print(f"  [Device] {r}")

    def rotate(self, angle=None):
        cmd = f"/rotate {angle}" if angle else "/rotate"
        print(f"\n[Command] Toggling/setting orientation ({cmd})...")
        resp = self.send_raw(cmd)
        for r in resp:
            print(f"  [Device] {r}")

    def run_demo(self):
        print("\n==========================================")
        print("  Running Multi-Bubble Chat Showcase Demo")
        print("==========================================")
        self.clear()
        time.sleep(2)

        self.send_user_message("Hello! Can you help me with my task today?")
        time.sleep(2.5)

        self.send_agent_message("Hello! I am your AI assistant on the e-Paper display. How can I help you?")
        time.sleep(2.5)

        self.send_user_message("Can you display messages with word-wrapping and chat bubbles on opposing sides?")
        time.sleep(2.5)

        self.send_agent_message("Yes! User messages appear on the right side in high-contrast solid bubbles, while agent responses appear on the left with clean outlined bubbles.")
        print("\nDemo completed successfully!")


def interactive_chat(client):
    print("\n" + "=" * 60)
    print("  ESP32 e-Paper Interactive Chat Console (Vertical Mode)")
    print("=" * 60)
    print("Commands:")
    print("  Type any message       -> Sent as USER message (Right side)")
    print("  /agent <msg> or /a ... -> Sent as AGENT message (Left side)")
    print("  /clear                 -> Clears chat history on screen")
    print("  /rotate                -> Toggles 180° rotation (90° / 270°)")
    print("  /demo                  -> Runs sample multi-turn dialogue")
    print("  /quit or exit          -> Exits console")
    print("-" * 60 + "\n")

    try:
        while True:
            try:
                line = input("Chat > ").strip()
            except EOFError:
                break

            if not line:
                continue

            if line.lower() in ("exit", "quit", "/quit", "/exit", "q"):
                print("Exiting interactive chat.")
                break
            elif line.startswith("/agent ") or line.startswith("/a "):
                parts = line.split(" ", 1)
                if len(parts) > 1:
                    client.send_agent_message(parts[1].strip())
            elif line.startswith("/user ") or line.startswith("/u "):
                parts = line.split(" ", 1)
                if len(parts) > 1:
                    client.send_user_message(parts[1].strip())
            elif line == "/clear":
                client.clear()
            elif line.startswith("/rotate"):
                parts = line.split()
                angle = parts[1] if len(parts) > 1 else None
                client.rotate(angle)
            elif line == "/demo":
                client.run_demo()
            else:
                # Default: send as user message
                client.send_user_message(line)

    except KeyboardInterrupt:
        print("\nInterrupted by user.")


def main():
    parser = argparse.ArgumentParser(
        description="Send chat messages to Waveshare ESP32-S3 e-Paper display in vertical chat bubble format."
    )
    parser.add_argument("-u", "--user", type=str, help="Send a user message (displayed on RIGHT side in black bubble)")
    parser.add_argument("-a", "--agent", type=str, help="Send an agent message (displayed on LEFT side in white bubble)")
    parser.add_argument("-p", "--port", type=str, help="Serial port (default: auto-detected /dev/cu.usbmodem*)")
    parser.add_argument("--clear", action="store_true", help="Clear chat history and reset display screen")
    parser.add_argument("--rotate", nargs="?", const="toggle", help="Toggle rotation or set angle: 90 or 270")
    parser.add_argument("--demo", action="store_true", help="Run automated multi-bubble conversation demo")
    parser.add_argument("-i", "--interactive", action="store_true", help="Start interactive chat console")

    args = parser.parse_args()

    client = ESP32ChatClient(port=args.port)
    try:
        client.connect()

        if args.clear:
            client.clear()

        if args.rotate:
            angle = None if args.rotate == "toggle" else args.rotate
            client.rotate(angle)

        if args.demo:
            client.run_demo()
            return

        if args.user:
            client.send_user_message(args.user)

        if args.agent:
            client.send_agent_message(args.agent)

        if args.interactive or (not args.user and not args.agent and not args.clear and not args.rotate and not args.demo):
            interactive_chat(client)

    finally:
        client.close()


if __name__ == "__main__":
    main()
