# ESP32-S3-ePaper-3.97-glosos

Unified Composite Firmware, display drivers, and real-time USB audio tools for the **Waveshare ESP32-S3 3.97" e-Paper Display** board, powered by the **ES8311 I2S Audio Codec**, onboard NS4150B class-D amplifier, and 800×480 electronic paper display.

---

## 📋 Features

- 🔊 **Composite USB Stack (Audio + Chat Serial concurrently)**
  - **Single USB Cable**: The ESP32-S3 enumerates as a standard USB Composite Device featuring **both** a native USB Audio Class (UAC) sound card and a USB CDC Serial (virtual COM) port over the single USB connection.
  - **Standard ALSA / UAC Sound Card (Linux / macOS / Windows)**: Recognized natively by Linux `snd-usb-audio` (`arecord` & `aplay`), macOS CoreAudio, and Windows without custom drivers.
  - **e-Paper Chat Serial Port**: Virtual COM port recognized on Linux (`/dev/ttyACM*`) and macOS (`/dev/cu.usbmodem*`) for posting user & agent chat messages directly to the screen via `send_chat.py`.
  - **ES8311 Codec & NS4150B PA Hardware Driver**: Full I2C initialization (`0x18`), hardware PA enable on GPIO 39, master clock generation (12.288 MHz on GPIO 13), and full-duplex I2S audio.
  - **ALSA Mixer Hardware Volume & Mute**: Hardware volume sliders and mute controls in `alsamixer` or system settings control the ES8311 DAC and PA directly.
  - **macOS CoreAudio Support**: Configured with asynchronous feedback endpoint (`CONFIG_UAC_SUPPORT_MACOS=y`) to ensure clean clock synchronization across macOS and Linux kernels.
  - **Boot Chime**: Hardware self-test tone on power-up confirms codec and amplifier function immediately.

- 🖼️ **e-Paper Chat Display Engine (`audio_firmware` & `display_firmware`)**
  - **Vertical (Portrait) Orientation**: 480 × 800 layout with chat bubbles and clean styling.
  - **Opposing Chat Bubbles**:
    - **User messages**: Right-aligned solid black bubbles with white text.
    - **Agent messages**: Left-aligned crisp outlined bubbles with black text.
  - **Word Wrapping & Auto-Layout**: Dynamic text wrapping with Font24 (17×24 px bitmap) typography.
  - **Automatic Scrolling**: Keeps newest messages in view as chat history grows.
  - **Fast Refresh (1.5s)**: Fast e-Paper updates with minimal flicker.
  - **Rotation Control**: Instantly flip 180° (90° / 270°) via software command (`/rotate`) or onboard BOOT button (GPIO 0).

- 💻 **Host Tools & Testing**
  - **`send_chat.py`**: Send user & agent chat messages from host over USB, trigger demos, clear screen, or run an interactive terminal chat console. Auto-detects port on both Linux and macOS.
  - **`test_alsa.sh`**: One-line testing script for Linux (OpenStick / Raspberry Pi / Ubuntu) for microphone record, speaker playback, sine tone, and mixer controls.

---

## 🔌 Hardware Pinout

### Audio (ES8311 Codec & Power Amplifier)

| Function | ESP32-S3 Pin | Notes |
| :--- | :--- | :--- |
| **I2C SDA** | `GPIO 41` | Codec control (Address: `0x18`) |
| **I2C SCL** | `GPIO 42` | Shared with onboard RTC & sensors |
| **I2S MCLK** | `GPIO 13` | Master Clock (I2S Master) |
| **I2S BCLK** | `GPIO 14` | Bit Clock |
| **I2S WS (LRCK)** | `GPIO 47` | Word Select / Frame Clock |
| **I2S DOUT** | `GPIO 48` | Serial Data Out (ESP32 → Codec Speaker) |
| **I2S DIN** | `GPIO 21` | Serial Data In (Codec Microphone → ESP32) |
| **PA_CTRL** | `GPIO 39` | Power Amplifier Enable (`HIGH` = On, `LOW` = Off) |

### Physical Buttons & Controls

| Input | ESP32-S3 Pin | Active Level | Default Function |
| :--- | :--- | :--- | :--- |
| **BOOT Button** | `GPIO 0` | `LOW` (Pull-Up) | Bootloader mode (at reset) / Toggle 180° orientation (runtime) |
| **Rotary Center Press** | `GPIO 5` | `LOW` (Pull-Up) | Input control |
| **Rotary Up** | `GPIO 4` | `LOW` (Pull-Up) | Configurable navigation input |
| **Rotary Down** | `GPIO 6` | `LOW` (Pull-Up) | Configurable navigation input |

### e-Paper Display (SPI)

| Function | ESP32-S3 Pin | Notes |
| :--- | :--- | :--- |
| **SPI SCK** | `GPIO 11` | SPI Clock |
| **SPI MOSI** | `GPIO 12` | SPI Master Out |
| **CS** | `GPIO 10` | Chip Select |
| **DC** | `GPIO 9` | Data / Command Control |
| **RST** | `GPIO 46` | Hardware Reset |
| **BUSY** | `GPIO 3` | Busy Status Input |

---

## 📁 Repository Structure

```
ESP32-S3-ePaper-3.97-glosos/
├── audio_firmware/               # ESP-IDF v5.x project: Unified Composite Firmware (Audio + Chat)
│   ├── CMakeLists.txt
│   ├── build.sh                  # Build script
│   ├── flash.sh                  # Flash script (detects /dev/cu.usbmodem* and /dev/ttyACM*)
│   ├── sdkconfig.defaults        # Composite UAC+CDC, macOS CoreAudio support, 16kHz audio
│   └── main/
│       ├── main.cpp              # Audio engine, PA, Boot chime, CDC chat listener, Button task
│       ├── usb_descriptors.c     # USB Composite Descriptors (CDC Serial + UAC Audio)
│       ├── tusb_config.h         # TinyUSB configuration (CFG_TUD_CDC=1, CFG_TUD_AUDIO=1)
│       ├── chat_display.h/.cpp   # Chat bubble layout, word wrap, scroll, and e-Paper refresh
│       ├── EPD_3in97.h/.cpp      # Waveshare 3.97" e-Paper display driver
│       ├── GUI_Paint.h/.cpp      # Framebuffer drawing library
│       ├── DEV_Config.h/.c       # Hardware SPI & GPIO abstraction for e-Paper
│       └── fonts.h/.cpp          # Font24 bitmap table
├── display_firmware/             # PlatformIO project: Standalone 3.97" e-Paper Chat firmware
├── test_alsa.sh                  # Linux ALSA automated test script (record, play, tone, info)
├── send_chat.py                  # Host tool: send user & agent chat bubbles to display over USB
└── README.md
```

---

## 🚀 Quick Start (Unified Composite Firmware)

### 1. Build and Flash `audio_firmware`

The firmware is built with ESP-IDF v5.x.

#### Build:
```bash
./audio_firmware/build.sh
```

#### Flash:
Put the board into ROM bootloader mode:
1. Hold down **BOOT** (GPIO 0).
2. Tap **RESET**.
3. Release **BOOT**.

Then run:
```bash
./audio_firmware/flash.sh
```

Upon boot, the board will:
1. Play a 4-note startup test chime through the onboard speaker.
2. Initialize the e-Paper display in vertical portrait mode.
3. Enumerate on USB simultaneously as:
   - **ALSA Sound Card** (`ESP UAC Device`)
   - **Serial Port** (`/dev/ttyACM0` on Linux, `/dev/cu.usbmodem*` on macOS)

---

### 2. Display Chat Messages on the e-Paper Screen

While the board is plugged in, use `send_chat.py` from your host computer:

```bash
# Send a User bubble (black bubble on right side):
python3 send_chat.py --user "Hello! Can you hear me?"

# Send an Agent bubble (outlined bubble on left side):
python3 send_chat.py --agent "Yes, I am listening and recording your audio."

# Run the showcase demo:
python3 send_chat.py --demo

# Launch interactive chat console:
python3 send_chat.py --interactive

# Clear screen history:
python3 send_chat.py --clear

# Toggle screen orientation 180°:
python3 send_chat.py --rotate
# (Or press the physical BOOT button on the board)
```

#### No-Python Environment (e.g. OpenStick / Minimal Linux):
Compile and use the standalone C binary [`send_chat_raw.c`](file:///Users/evgeniibasistyi/Documents/GitHub/ESP32-S3-ePaper-3.97-glosos/send_chat_raw.c), which communicates directly over raw USB usbfs without requiring Python or the `cdc_acm` kernel module:

```bash
gcc -O2 send_chat_raw.c -o send_chat
./send_chat "/agent Hello from pure C!"
./send_chat "/user Testing without Python"
./send_chat "/demo"
./send_chat "/rotate"
./send_chat "/clear"
```

---

### 3. Record & Play Audio (Linux / ALSA)

On Linux (`openstick-debian`, Raspberry Pi, Ubuntu, etc.):

#### Check Sound Card:
```bash
aplay -l
arecord -l
# Shows: card 1: Device [ESP UAC Device], device 0: USB Audio [USB Audio]
```

#### Run Automated Test (`test_alsa.sh`):
```bash
# Loopback: records 4s from microphone, then plays through the speaker
./test_alsa.sh loopback

# Play a 1 kHz test beep
./test_alsa.sh tone

# Record 5s of voice to test_esp32_audio.wav
./test_alsa.sh record 5

# Play a WAV file to the speaker
./test_alsa.sh play test_esp32_audio.wav
```

#### Manual Commands:
```bash
# Record from microphone:
arecord -D plughw:CARD=Device,DEV=0 -f S16_LE -r 16000 -c 1 -d 5 mic.wav

# Play to speaker:
aplay -D plughw:CARD=Device,DEV=0 mic.wav

# Hardware mixer (volume & mute):
alsamixer -c Device
```

---

### 4. Record & Play Audio (macOS)

On macOS, the board appears in **System Settings > Sound** as `ESP UAC Device` for both Input and Output.
- Select it as default speaker to play system sound or music.
- Select it as default mic in QuickTime, Audacity, or Python `sounddevice`.

---

## 📜 License

MIT License. See [LICENSE](LICENSE) for details.
