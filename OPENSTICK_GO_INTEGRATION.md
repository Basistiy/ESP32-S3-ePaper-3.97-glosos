# ESP32-S3 e-Paper Audio & Display Integration for Go

This document covers how Go applications running on OpenStick Linux communicate with the ESP32-S3 board over USB for microphone capture, speaker playback, and e-Paper chat display control.

---

## 1. Audio Specifications

Both the onboard microphone and speaker operate via standard ALSA:

* **ALSA Device**: `plughw:CARD=Display,DEV=0`
* **Sample Rate**: `16000` (16 kHz)
* **Sample Format**: `S16_LE` (16-bit Signed Little Endian)
* **Channels**: `1` (Mono)

---

## 2. Speaker Playback

### Play WAV File or Stream Raw PCM Bytes

```go
package main

import (
	"bytes"
	"os/exec"
)

// PlayWAV plays an existing WAV file through the onboard speaker
func PlayWAV(filePath string) error {
	cmd := exec.Command("aplay", "-D", "plughw:CARD=Display,DEV=0", filePath)
	return cmd.Run()
}

// PlayRawPCM streams 16kHz 16-bit mono PCM bytes to the speaker
func PlayRawPCM(pcmData []byte) error {
	cmd := exec.Command("aplay",
		"-D", "plughw:CARD=Display,DEV=0",
		"-f", "S16_LE",
		"-r", "16000",
		"-c", "1",
	)
	cmd.Stdin = bytes.NewReader(pcmData)
	return cmd.Run()
}
```

---

## 3. Microphone Capture

### Record to File

```go
package main

import (
	"os/exec"
	"strconv"
)

// RecordToFile records from the microphone for durationSeconds to outPath
func RecordToFile(durationSeconds int, outPath string) error {
	cmd := exec.Command("arecord",
		"-D", "plughw:CARD=Display,DEV=0",
		"-f", "S16_LE",
		"-r", "16000",
		"-c", "1",
		"-d", strconv.Itoa(durationSeconds),
		outPath,
	)
	return cmd.Run()
}
```

### Stream Live PCM Audio (for Speech-to-Text)

```go
package main

import (
	"io"
	"os/exec"
)

// StreamMic returns an io.ReadCloser streaming live 16kHz 16-bit mono PCM bytes
func StreamMic() (io.ReadCloser, *exec.Cmd, error) {
	cmd := exec.Command("arecord",
		"-D", "plughw:CARD=Display,DEV=0",
		"-f", "S16_LE",
		"-r", "16000",
		"-c", "1",
		"-t", "raw",
	)
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return nil, nil, err
	}
	if err := cmd.Start(); err != nil {
		return nil, nil, err
	}
	return stdout, cmd, nil
}
```

---

## 4. e-Paper Display Control

The display driver accepts text commands over the USB link via `/usr/local/bin/send_chat`.

### Command Reference

| Command | Action |
| :--- | :--- |
| `/user <text>` | Displays a right-aligned User message bubble |
| `/agent <text>` | Displays a left-aligned Agent message bubble |
| `/beep` | Triggers immediate hardware test tone (880 Hz, 300 ms) |
| `/clear` | Clears conversation history and refreshes screen |
| `/rotate` | Flips screen orientation 180° |

### Go Client Implementation

```go
package main

import (
	"fmt"
	"os/exec"
	"strings"
)

type DisplayClient struct{}

func (d *DisplayClient) Send(cmd string) (string, error) {
	out, err := exec.Command("send_chat", cmd).Output()
	if err != nil {
		return "", err
	}
	return strings.TrimSpace(string(out)), nil
}

func (d *DisplayClient) UserMessage(text string) error {
	_, err := d.Send(fmt.Sprintf("/user %s", text))
	return err
}

func (d *DisplayClient) AgentMessage(text string) error {
	_, err := d.Send(fmt.Sprintf("/agent %s", text))
	return err
}

func (d *DisplayClient) Beep() error {
	_, err := d.Send("/beep")
	return err
}

func (d *DisplayClient) Clear() error {
	_, err := d.Send("/clear")
	return err
}

func (d *DisplayClient) Rotate() error {
	_, err := d.Send("/rotate")
	return err
}
```

---

## 5. Complete Pipeline Example (`main.go`)

```go
package main

import (
	"fmt"
	"log"
	"os/exec"
	"time"
)

func main() {
	display := &DisplayClient{}

	// 1. Play alert beep and clear display
	if err := display.Beep(); err != nil {
		log.Printf("Beep error: %v", err)
	}
	display.Clear()
	time.Sleep(300 * time.Millisecond)

	// 2. Display agent prompt
	display.AgentMessage("Listening for input...")

	// 3. Record 4 seconds from onboard microphone
	recFile := "/tmp/user_input.wav"
	recCmd := exec.Command("arecord",
		"-D", "plughw:CARD=Display,DEV=0",
		"-f", "S16_LE", "-r", "16000", "-c", "1", "-d", "4",
		recFile,
	)
	if err := recCmd.Run(); err != nil {
		log.Fatalf("Microphone recording failed: %v", err)
	}

	// 4. Update display with transcribed message
	display.UserMessage("Hello from OpenStick Go service!")

	// 5. Display agent response
	display.AgentMessage("Audio captured and processed successfully.")

	// 6. Play the captured audio through the speaker
	playCmd := exec.Command("aplay", "-D", "plughw:CARD=Display,DEV=0", recFile)
	if err := playCmd.Run(); err != nil {
		log.Fatalf("Speaker playback failed: %v", err)
	}

	fmt.Println("Audio and display pipeline executed successfully.")
}
```
