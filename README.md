# ESP32-S3-ePaper-3.97-glosos

Firmware, display drivers, and real-time USB audio streaming tools for the **Waveshare ESP32-S3 3.97" e-Paper Display** board, powered by the **ES8311 I2S Audio Codec**, onboard class-D amplifier, and 800×480 electronic paper display.

---

## 📋 Features

- 🔊 **Audio Subsystem (`speaker_test`)**
  - **ES8311 Codec Control**: Full initialization over I2C, volume control, microphone gain, and clock management.
  - **Onboard Power Amplifier**: GPIO 39 active-high PA control.
  - **I2S Master Clocking**: 24 kHz 16-bit audio with MCLK output on GPIO 13.
  - **Microphone Capture**: Low-noise 24 kHz 16-bit voice recording via ES8311 ADC and I2S DIN (GPIO 21).
  - **Push-to-Talk & Tap-to-Record**: Trigger recording using onboard **BOOT button (GPIO 0)** or **Rotary Wheel press (GPIO 5)**.
  - **Real-Time USB Audio Streaming**: Stream audio bidirectionally (Mac → Speaker or Microphone → Mac) over native USB CDC.
  - **Packet Framing Protocol**: Custom `0xAA 0x55` sync framing that eliminates byte alignment errors and white-noise static.
  - **Onboard Synthesizer & Replay**: Pure sine wave tone generator, chime melodies, flash music, and instant replay of recorded voice.

- 🖼️ **e-Paper Display (`display_firmware`)**
  - **Waveshare 3.97" e-Paper**: 800×480 resolution driver.
  - **Dual Mode Support**: Fast 1-bit Black/White mode and 4-Level Grayscale mode.
  - **Zero Idle Power**: Automatic deep sleep latching after refresh.

- 💻 **Mac Host Audio Streaming & Recording Tools**
  - **`record_from_esp32.py`**: Listen for device button presses over USB, capture audio stream to standard `.wav`, and immediately play it on Mac speakers via macOS `afplay`.
  - **`stream_to_esp32.py`**: Stream any sound, speech, or music file (**MP3**, **WAV**, **AIFF**, or macOS TTS `say`) from Mac to the ESP32 speaker.

---

## 🔌 Hardware Pinout

### Audio (ES8311 Codec & Power Amplifier)

| Function | ESP32-S3 Pin | Notes |
| :--- | :--- | :--- |
| **I2C SDA** | `GPIO 41` | Codec control (Address: `0x18`) |
| **I2C SCL** | `GPIO 42` | Shared with onboard RTC & sensors |
| **I2S MCLK** | `GPIO 13` | Master Clock (24 kHz × 256 = 6.144 MHz) |
| **I2S BCLK** | `GPIO 14` | Bit Clock |
| **I2S WS (LRCK)** | `GPIO 47` | Word Select / Frame Clock |
| **I2S DOUT** | `GPIO 48` | Serial Data Out (ESP32 → Codec Speaker) |
| **I2S DIN** | `GPIO 21` | Serial Data In (Codec Microphone → ESP32) |
| **PA_CTRL** | `GPIO 39` | Power Amplifier Enable (`HIGH` = On, `LOW` = Off) |

### Physical Buttons & Controls

| Input | ESP32-S3 Pin | Active Level | Default Function |
| :--- | :--- | :--- | :--- |
| **BOOT Button** | `GPIO 0` | `LOW` (Pull-Up) | Hold to Record (Push-to-Talk) or Tap to Record |
| **Rotary Center Press** | `GPIO 5` | `LOW` (Pull-Up) | Hold to Record (Push-to-Talk) or Tap to Record |
| **Rotary Up** | `GPIO 4` | `LOW` (Pull-Up) | Configurable navigation input |
| **Rotary Down** | `GPIO 6` | `LOW` (Pull-Up) | Configurable navigation input |

### e-Paper Display (SPI)

| Function | ESP32-S3 Pin | Notes |
| :--- | :--- | :--- |
| **SPI SCK** | `GPIO 12` | SPI Clock |
| **SPI MOSI** | `GPIO 11` | SPI Master Out |
| **CS** | `GPIO 10` | Chip Select |
| **DC** | `GPIO 9` | Data / Command Control |
| **RST** | `GPIO 8` | Hardware Reset |
| **BUSY** | `GPIO 7` | Busy Status Input |
| **PWR** | `GPIO 6` | Display Power Switch |

---

## 📁 Repository Structure

```
ESP32-S3-ePaper-3.97-glosos/
├── speaker_test/                 # PlatformIO project: Audio codec, PA & streaming receiver
│   ├── platformio.ini
│   └── src/
│       ├── main.cpp              # Audio engine, test tones & packet stream receiver
│       ├── es8311.cpp            # ES8311 driver implementation
│       ├── es8311.h              # ES8311 register & API definitions
│       ├── es8311_reg.h          # ES8311 hardware registers
│       └── music.h               # Flash PCM music sample (14.5s)
├── display_firmware/             # PlatformIO project: 3.97" e-Paper display firmware
│   ├── platformio.ini
│   └── src/
│       ├── main.cpp              # Display demo (1-bit / 4-level grayscale)
│       ├── EPD_3in97.cpp         # 3.97" e-Paper controller
│       ├── EPD_3in97.h
│       ├── DEV_Config.cpp        # Hardware SPI & GPIO abstraction
│       ├── DEV_Config.h
│       └── image_data.h          # Sample e-Paper bitmap images
├── images/                       # Sample converted e-Paper images
│   ├── fit_entire_1bit.png
│   ├── fit_entire_4gray.png
│   ├── prominent_1bit.png
│   ├── prominent_4gray.png
│   └── ...
├── record_from_esp32.py          # Mac tool: records from ESP32 mic over USB & plays on Mac
├── stream_to_esp32.py            # Mac streaming script (FFmpeg / macOS TTS over USB)
├── glosos_commands.MP3           # Sample voice command audio track
└── README.md
```

---

## 🚀 Quick Start

### Prerequisites

- [PlatformIO](https://platformio.org/) installed (`pio` CLI or VS Code extension)
- Python 3 with `pyserial`:
  ```bash
  pip install pyserial
  ```
- [FFmpeg](https://ffmpeg.org/) (for converting audio files on the fly when streaming to ESP32):
  ```bash
  brew install ffmpeg
  ```

---

### 1. Flash the Audio & Microphone Firmware

Navigate to the `speaker_test` project and upload:

```bash
cd speaker_test
pio run -t upload
```

*(PlatformIO automatically detects the `/dev/cu.usbmodem*` port on macOS.)*

When the board boots, it initializes the codec, turns on the power amplifier on GPIO 39, and plays a clean 4-note chime.

#### Interactive Serial Commands (115200 baud)

Open the serial monitor:
```bash
pio device monitor
```
Or send single keystrokes:
| Key / Action | Action |
| :---: | :--- |
| **BOOT / Rotary Press** | **Record audio from microphone and stream to Mac** (Push-to-Talk or Tap) |
| `R` | Trigger microphone audio recording & stream over USB |
| `4` | Replay last recorded audio buffer on the onboard speaker |
| `S` | Enter USB audio streaming receiver mode (from Mac) |
| `1` | Play 3 diagnostic test beeps (440 Hz, 880 Hz, 1760 Hz) |
| `2` | Play boot chime melody (C5 – E5 – G5 – C6) |
| `3` | Play the ~14.5s onboard PCM music sample |
| `+` | Increase speaker volume (+5%) |
| `-` | Decrease speaker volume (-5%) |
| `g` | Increase microphone gain (+6 dB) |
| `G` | Decrease microphone gain (-6 dB) |
| `p` | Toggle Power Amplifier (`PA_CTRL`) on/off |

---

### 2. Record Audio with Device Button & Stream to Mac

Run the included [`record_from_esp32.py`](record_from_esp32.py) listener tool on your Mac:

```bash
python3 record_from_esp32.py
```

The tool connects to the ESP32 and waits for button presses:
1. **Push-to-Talk**: Hold down the **BOOT button (GPIO 0)** or **Rotary Wheel (GPIO 5)** on the device while speaking, then release to finish.
2. **Tap-to-Record**: Tap either button once to record for 5 seconds (or tap again to stop early).
3. **Instant Playback**: As soon as recording completes, the audio is saved to `recording_<timestamp>.wav` and immediately played through your Mac's speakers using macOS native `afplay`.

#### Useful Flags:
```bash
# Save to a specific WAV file name:
python3 record_from_esp32.py -o my_recording.wav

# Record once and exit immediately:
python3 record_from_esp32.py --once

# Trigger recording directly from your Mac without touching the board:
python3 record_from_esp32.py --trigger

# Record and also replay on the ESP32's onboard speaker:
python3 record_from_esp32.py --play-on-device
```

---

### 3. Stream Audio from Mac to ESP32 Speaker

Use the included [`stream_to_esp32.py`](stream_to_esp32.py) script to stream any sound over USB in real time:

#### Speak Text (macOS Text-to-Speech)
```bash
python3 stream_to_esp32.py --say "Hello! Voice audio is streaming to the ESP32 speaker."
```

#### Play an MP3 File
```bash
python3 stream_to_esp32.py glosos_commands.MP3
```

#### Play macOS System Sounds
```bash
python3 stream_to_esp32.py /System/Library/Sounds/Hero.aiff
python3 stream_to_esp32.py /System/Library/Sounds/Glass.aiff
python3 stream_to_esp32.py /System/Library/Sounds/Sosumi.aiff
```

---

### 4. Flash the e-Paper Display Firmware

To test the 3.97-inch e-paper display:

```bash
cd display_firmware
pio run -t upload
```

In `display_firmware/src/main.cpp`, you can select between:
- `#define USE_4GRAY 1` (4-Level Grayscale for photos/artwork)
- `#define USE_4GRAY 0` (Fast 1-Bit Black & White mode)

Once updated, the e-paper permanently retains the image even after removing power.

---

## 🔬 Technical Note: The Framing Protocol & White Noise Fix

In 16-bit little-endian PCM, each sample is 2 bytes: `[Low Byte, High Byte]`.
- Low byte = least significant bits (dither / fine volume)
- High byte = most significant bits & sign bit (waveform amplitude)

When transmitting over raw serial streams, a single leftover byte (such as a trailing `\n` from a command) will phase-shift the entire stream by 1 byte. The low byte is interpreted as the high byte, causing the signal to jump between positive and negative extremes at 24 kHz, creating **100% full-scale white noise**.

To solve this permanently, the firmware and Python streamer implement a **framed packet protocol**:
```
+-------------------+----------------------+-----------------------------+
| 0xAA 0x55 (Sync)  | uint16_t SampleCount | SampleCount x int16_t (PCM) |
+-------------------+----------------------+-----------------------------+
```
- The ESP32 flushes its receive buffer before starting.
- Incoming bytes are searched for the `0xAA 0x55` sync header.
- Sample boundaries are strictly preserved, making byte-shift distortion mathematically impossible.
- An end-of-stream packet (`0xAA 0x55 0x00 0x00`) drains the DMA buffer cleanly without clipping.

---

## 📜 License

MIT License. See [LICENSE](LICENSE) for details.
