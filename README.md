# ESP32-S3-ePaper-3.97-glosos

Firmware, display drivers, and real-time USB audio streaming tools for the **Waveshare ESP32-S3 3.97" e-Paper Display** board, powered by the **ES8311 I2S Audio Codec**, onboard NS4150B class-D amplifier, and 800×480 electronic paper display.

---

## 📋 Features

- 🔊 **Native USB Audio Class Subsystem (`audio_firmware`)**
  - **Standard ALSA / UAC Sound Card (Linux / macOS / Windows)**: Enumerates natively as a standard USB Audio Class device recognized automatically by Linux `snd-usb-audio` (`arecord` & `aplay`), macOS CoreAudio, and Windows without custom drivers.
  - **ES8311 Codec & NS4150B PA Hardware Driver**: Full I2C initialization (0x18), hardware PA enable on GPIO 39, master clock generation (12.288 MHz on GPIO 13), and full-duplex I2S audio.
  - **ALSA Mixer Hardware Volume & Mute**: Hardware volume sliders and mute controls in `alsamixer` or system settings control the ES8311 DAC and PA directly.
  - **macOS CoreAudio Support**: Configured with asynchronous feedback endpoint (`CONFIG_UAC_SUPPORT_MACOS=y`) to ensure clean clock synchronization across macOS and Linux kernels.
  - **Boot Chime**: Hardware self-test tone on power-up confirms codec and amplifier function immediately.

- 🖼️ **e-Paper Chat Display Subsystem (`display_firmware`)**
  - **Vertical (Portrait) Orientation**: 480 × 800 layout with chat bubbles and clean styling.
  - **Opposing Chat Bubbles**:
    - **User messages**: Right-aligned solid black bubbles with white text.
    - **Agent messages**: Left-aligned crisp outlined bubbles with black text.
  - **Word Wrapping & Auto-Layout**: Dynamic text wrapping with Font24 (17×24 px bitmap) typography.
  - **Automatic Scrolling**: Keeps newest messages in view as chat history grows.
  - **Fast Refresh (1.5s)**: Fast e-Paper updates with minimal flicker.
  - **Rotation Control**: Instantly flip 180° (90° / 270°) via software command or onboard BOOT button.

- 💻 **Host Tools & Testing**
  - **`test_alsa.sh`**: One-line testing script for Linux (OpenStick / Raspberry Pi / Ubuntu) for microphone record, speaker playback, sine tone, and mixer controls.
  - **`send_chat.py`**: Send user & agent chat messages from host over USB, trigger demos, clear screen, or run an interactive terminal chat console.
  - **`record_from_esp32.py`** & **`stream_to_esp32.py`**: Companion host scripts for legacy streaming modes.

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
| **BOOT Button** | `GPIO 0` | `LOW` (Pull-Up) | Bootloader / Push-to-Talk / Rotate |
| **Rotary Center Press** | `GPIO 5` | `LOW` (Pull-Up) | Push-to-Talk or Tap to Record |
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
├── audio_firmware/               # ESP-IDF v5.x project: Native USB Audio Class (UAC/ALSA) firmware
│   ├── CMakeLists.txt
│   ├── build.sh                  # Build script (uses esp-idf export.sh)
│   ├── flash.sh                  # Flash script (detects /dev/cu.usbmodem*)
│   ├── sdkconfig.defaults        # UAC, macOS CoreAudio support, sample rate config
│   ├── sdkconfig.defaults.esp32s3# Waveshare pin mapping (I2C 41/42, I2S 13/14/47/48/21, PA 39)
│   └── main/
│       ├── idf_component.yml     # Managed dependencies (usb_device_uac, esp_codec_dev)
│       └── main.c                # Hardware PA init, boot chime, UAC callbacks & mixer
├── display_firmware/             # PlatformIO project: Standalone 3.97" e-Paper Chat firmware
│   ├── platformio.ini
│   └── src/
│       ├── main.cpp              # Interactive Chat terminal entry point
│       ├── chat_display.h
│       ├── chat_display.cpp
│       ├── EPD_3in97.cpp
│       ├── EPD_3in97.h
│       └── DEV_Config.cpp
├── test_alsa.sh                  # Linux ALSA automated test script (record, play, tone, info)
├── send_chat.py                  # Host tool: send user & agent chat bubbles to display over USB
├── glosos_commands.MP3           # Sample voice command audio track
└── README.md
```

---

## 🚀 Native USB Audio (ALSA) Quick Start

### 1. Build and Flash `audio_firmware`

The audio firmware is built with ESP-IDF v5.x using official Espressif components (`usb_device_uac` and `esp_codec_dev`).

#### Prerequisites:
- ESP-IDF v5.3+ installed (e.g. `~/esp/esp-idf`).

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
Or specify the port explicitly:
```bash
PORT=/dev/cu.usbmodem1101 ./audio_firmware/flash.sh
```

Upon boot, the board plays a startup test chime to verify the ES8311 codec and NS4150B amplifier.

---

### 2. Using with Linux (ALSA / OpenStick / Raspberry Pi)

Plug the board into your Linux host. It will enumerate automatically as `ESP UAC Device`:

#### Inspect Audio Devices:
```bash
aplay -l
arecord -l
```
You will see:
```text
card 1: Device [ESP UAC Device], device 0: USB Audio [USB Audio]
```

#### Automated Test (`test_alsa.sh`):
```bash
# Loopback test: records 4s from microphone, then plays it through the speaker
./test_alsa.sh loopback

# Play a 1 kHz test tone through the speaker
./test_alsa.sh tone

# Record 5 seconds of microphone audio
./test_alsa.sh record 5

# Play a WAV file through the speaker
./test_alsa.sh play my_file.wav

# View ALSA card info and hardware mixer controls
./test_alsa.sh info
```

#### Manual ALSA Commands:
```bash
# Record from microphone:
arecord -D plughw:CARD=Device,DEV=0 -f S16_LE -r 16000 -c 1 -d 5 test_mic.wav

# Play to speaker:
aplay -D plughw:CARD=Device,DEV=0 test_mic.wav

# Open hardware volume and mute mixer:
alsamixer -c Device
```

---

### 3. Using with macOS (CoreAudio)

On macOS, the board appears in **System Settings > Sound** as `ESP UAC Device` for both Input and Output.

- You can select it as the default output device and play system audio, music, or speech.
- You can select it as the default input device in any recording application (QuickTime, Audacity, Python `sounddevice`).

---

## 🖼️ e-Paper Chat Display Quick Start

### 1. Flash `display_firmware`

Navigate to `display_firmware` and upload using PlatformIO:

```bash
cd display_firmware
pio run -t upload
```

### 2. Send Chat Messages

```bash
# Send a User bubble (black bubble on right side):
python3 send_chat.py --user "Hello! Can you hear me?"

# Send an Agent bubble (outlined bubble on left side):
python3 send_chat.py --agent "Yes, I am receiving your audio."

# Interactive chat console:
python3 send_chat.py --interactive

# Clear screen history:
python3 send_chat.py --clear

# Rotate screen 180°:
python3 send_chat.py --rotate
```

---

## 📜 License

MIT License. See [LICENSE](LICENSE) for details.
