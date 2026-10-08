#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "driver/i2s.h"
#include "esp_heap_caps.h"
#include "es8311.h"
#include "music.h"
#include "chat_display.h"

// Hardware Pin Definitions
#define I2C_SDA_PIN      41
#define I2C_SCL_PIN      42

#define I2S_NUM          I2S_NUM_0
#define I2S_MCK_PIN      13
#define I2S_BCK_PIN      14
#define I2S_LRCK_PIN     47
#define I2S_DOUT_PIN     48
#define I2S_DIN_PIN      21
#define PA_CTRL_PIN      39

// Hardware Button Definitions (Active LOW with internal pull-ups)
#define BUTTON_BOOT_PIN   0   // Onboard BOOT button
#define BUTTON_WHEEL_PIN  5   // Onboard Rotary Wheel Center/Function click
#define BUTTON_UP_PIN     4   // Rotary Up
#define BUTTON_DOWN_PIN   6   // Rotary Down

#define SAMPLE_RATE      24000
#define MCLK_MULTIPLE    256
#define MCLK_FREQ_HZ     (SAMPLE_RATE * MCLK_MULTIPLE)

static es8311_handle_t es_handle = NULL;
static int current_volume = 80;
static int current_mic_gain = ES8311_MIC_GAIN_30DB; // +30dB
static bool pa_enabled = true;

// PSRAM Recording Buffer (stores up to 15 seconds of 24kHz 16-bit mono PCM)
static int16_t *g_recorded_audio = NULL;
static size_t g_max_record_samples = SAMPLE_RATE * 15;
static size_t g_recorded_count = 0;

void scanI2C() {
    Serial.println("\n--- Scanning I2C bus (SDA=41, SCL=42) ---");
    byte count = 0;
    for (byte addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        byte error = Wire.endTransmission();
        if (error == 0) {
            Serial.printf("  Found device at 0x%02X", addr);
            if (addr == 0x18) Serial.print(" (ES8311 Audio Codec)");
            else if (addr == 0x51) Serial.print(" (PCF85063 RTC)");
            else if (addr == 0x6B) Serial.print(" (QMI8658 IMU)");
            else if (addr == 0x70) Serial.print(" (SHTC3 Temp/Humid)");
            Serial.println();
            count++;
        }
    }
    if (count == 0) {
        Serial.println("  No I2C devices found!");
    } else {
        Serial.printf("  Total %d device(s) found.\n", count);
    }
    Serial.println("-----------------------------------------\n");
}

bool initCodec() {
    Serial.println("[1/3] Initializing ES8311 Codec over I2C...");
    es_handle = es8311_create(I2C_NUM_0, ES8311_ADDRRES_0);
    if (!es_handle) {
        Serial.println("  ERROR: es8311_create failed!");
        return false;
    }

    const es8311_clock_config_t es_clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = MCLK_FREQ_HZ,
        .sample_frequency = SAMPLE_RATE
    };

    esp_err_t err = es8311_init(es_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
    if (err != ESP_OK) {
        Serial.printf("  ERROR: es8311_init failed (0x%x)!\n", err);
        return false;
    }

    es8311_voice_volume_set(es_handle, current_volume, NULL);
    es8311_microphone_config(es_handle, false);
    es8311_microphone_gain_set(es_handle, (es8311_mic_gain_t)current_mic_gain);
    Serial.printf("  ES8311 initialized successfully. Volume: %d%%, Mic Gain: +%ddB\n",
                  current_volume, current_mic_gain * 6);
    return true;
}

bool initI2S() {
    Serial.println("[2/3] Initializing I2S peripheral (TX & RX)...");
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 512,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        .bits_per_chan = I2S_BITS_PER_CHAN_16BIT
    };

    esp_err_t err = i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("  ERROR: i2s_driver_install failed (0x%x)!\n", err);
        return false;
    }

    i2s_pin_config_t pin_config = {
        .mck_io_num = I2S_MCK_PIN,
        .bck_io_num = I2S_BCK_PIN,
        .ws_io_num = I2S_LRCK_PIN,
        .data_out_num = I2S_DOUT_PIN,
        .data_in_num = I2S_DIN_PIN
    };

    err = i2s_set_pin(I2S_NUM, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("  ERROR: i2s_set_pin failed (0x%x)!\n", err);
        return false;
    }

    Serial.println("  I2S driver installed (MCLK=13, BCK=14, WS=47, DOUT=48, DIN=21).");
    return true;
}

void playTone(float freq, int duration_ms, float volume = 0.5f) {
    int total_samples = (SAMPLE_RATE * duration_ms) / 1000;
    const int CHUNK = 256;
    int16_t buffer[CHUNK * 2];
    float phase = 0.0f;
    float phase_step = (2.0f * (float)M_PI * freq) / (float)SAMPLE_RATE;

    int samples_sent = 0;
    while (samples_sent < total_samples) {
        int count = min(CHUNK, total_samples - samples_sent);
        for (int i = 0; i < count; i++) {
            float env = 1.0f;
            int current_idx = samples_sent + i;
            if (current_idx < 120) env = (float)current_idx / 120.0f;
            else if (total_samples - current_idx < 120) env = (float)(total_samples - current_idx) / 120.0f;

            int16_t sample_val = (int16_t)(sinf(phase) * 30000.0f * volume * env);
            phase += phase_step;
            if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;

            buffer[i * 2]     = sample_val;
            buffer[i * 2 + 1] = sample_val;
        }
        size_t bytes_written = 0;
        i2s_write(I2S_NUM, buffer, count * sizeof(int16_t) * 2, &bytes_written, portMAX_DELAY);
        samples_sent += count;
    }
}

void playMelody() {
    Serial.println(">>> Playing chime melody...");
    struct Note { float freq; int dur; };
    Note melody[] = {
        { 523.25f, 150 }, // C5
        { 659.25f, 150 }, // E5
        { 783.99f, 150 }, // G5
        { 1046.50f, 350 }, // C6
    };
    for (const auto &note : melody) {
        playTone(note.freq, note.dur, 0.6f);
        delay(30);
    }
    Serial.println(">>> Chime melody finished.");
}

void playBeeps() {
    Serial.println(">>> Playing 3 diagnostic test beeps (440Hz, 880Hz, 1760Hz)...");
    playTone(440.0f, 200, 0.6f);
    delay(100);
    playTone(880.0f, 200, 0.6f);
    delay(100);
    playTone(1760.0f, 300, 0.6f);
    Serial.println(">>> Beeps finished.");
}

void playMusicTrack() {
    Serial.printf(">>> Playing sample music track (%u samples, ~14.5s)...\n", AUDIO_SAMPLES);
    const int CHUNK = 512;
    int16_t stereo_buf[CHUNK * 2];

    uint32_t samples_played = 0;
    while (samples_played < AUDIO_SAMPLES) {
        int count = min((uint32_t)CHUNK, AUDIO_SAMPLES - samples_played);
        for (int i = 0; i < count; i++) {
            int16_t val = (int16_t)audio_data[samples_played + i];
            stereo_buf[i * 2]     = val;
            stereo_buf[i * 2 + 1] = val;
        }
        size_t bytes_written = 0;
        i2s_write(I2S_NUM, stereo_buf, count * sizeof(int16_t) * 2, &bytes_written, portMAX_DELAY);
        samples_played += count;

        if (samples_played % (SAMPLE_RATE * 2) < CHUNK) {
            Serial.printf("    Progress: %u / %u samples (%.1f s)\n",
                          samples_played, AUDIO_SAMPLES, (float)samples_played / SAMPLE_RATE);
        }
    }
    Serial.println(">>> Music track playback finished.");
}

void handleStreamAudio() {
    // Flush any pending bytes in Serial RX buffer (removes newline from 'S\n')
    while (Serial.available()) {
        Serial.read();
    }
    Serial.println("STREAM_READY");

    const int CHUNK = 512;
    int16_t mono_buf[CHUNK];
    int16_t stereo_buf[CHUNK * 2];

    uint32_t last_rx_time = millis();
    uint32_t total_samples = 0;

    while (millis() - last_rx_time < 2500) {
        if (Serial.available() >= 4) {
            // Search for magic sync header 0xAA, 0x55
            uint8_t b1 = Serial.read();
            if (b1 != 0xAA) continue;
            uint8_t b2 = Serial.read();
            if (b2 != 0x55) continue;

            // Read 2-byte sample count
            uint8_t len_low = Serial.read();
            uint8_t len_high = Serial.read();
            uint16_t sample_count = len_low | (len_high << 8);

            // Sample count 0 means clean End-of-Stream
            if (sample_count == 0) {
                break;
            }
            if (sample_count > CHUNK) {
                sample_count = CHUNK;
            }

            // Read exactly sample_count * 2 bytes of PCM audio
            int bytes_to_read = sample_count * 2;
            int bytes_read = 0;
            uint32_t wait_start = millis();
            while (bytes_read < bytes_to_read && (millis() - wait_start < 500)) {
                if (Serial.available()) {
                    bytes_read += Serial.readBytes(((char*)mono_buf) + bytes_read, bytes_to_read - bytes_read);
                } else {
                    vTaskDelay(pdMS_TO_TICKS(1));
                }
            }

            int valid_samples = bytes_read / 2;
            for (int i = 0; i < valid_samples; i++) {
                stereo_buf[i * 2]     = mono_buf[i];
                stereo_buf[i * 2 + 1] = mono_buf[i];
            }

            size_t bytes_written = 0;
            i2s_write(I2S_NUM, stereo_buf, valid_samples * sizeof(int16_t) * 2, &bytes_written, portMAX_DELAY);
            total_samples += valid_samples;
            last_rx_time = millis();
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    Serial.printf("STREAM_DONE:%u\n", total_samples);
}

void recordAudioSession(bool triggered_by_button) {
    Serial.println("\n>>> Starting Audio Recording...");

    // Optional short start pip for auditory feedback
    if (pa_enabled) {
        digitalWrite(PA_CTRL_PIN, HIGH);
        playTone(1200.0f, 40, 0.3f);
        delay(20);
    }

    // Disable PA during recording to eliminate speaker hiss/feedback
    digitalWrite(PA_CTRL_PIN, LOW);
    delay(20);

    // Ensure microphone is properly configured with current gain
    if (es_handle) {
        es8311_microphone_config(es_handle, false);
        es8311_microphone_gain_set(es_handle, (es8311_mic_gain_t)current_mic_gain);
    }

    // Flush RX DMA buffer to remove any stale samples
    i2s_zero_dma_buffer(I2S_NUM);
    size_t dummy_bytes = 0;
    int16_t dummy_buf[256];
    for (int i = 0; i < 4; i++) {
        i2s_read(I2S_NUM, dummy_buf, sizeof(dummy_buf), &dummy_bytes, pdMS_TO_TICKS(30));
    }

    // Send recording start banner over USB Serial
    Serial.println("RECORD_START:24000:1:16");

    g_recorded_count = 0;
    const int CHUNK = 256;
    int16_t rx_stereo[CHUNK * 2];
    int16_t tx_mono[CHUNK];

    uint32_t start_time = millis();
    uint32_t max_duration_ms = 30000; // 30s maximum timeout
    int active_channel = -1; // Auto-detect 0 (Left) or 1 (Right)
    bool is_push_to_talk = false;
    bool button_was_released = false;

    while (millis() - start_time < max_duration_ms) {
        // Read 16-bit interleaved stereo samples from I2S
        size_t bytes_read = 0;
        esp_err_t err = i2s_read(I2S_NUM, rx_stereo, sizeof(rx_stereo), &bytes_read, pdMS_TO_TICKS(100));
        if (err != ESP_OK || bytes_read == 0) {
            continue;
        }

        int frames_read = bytes_read / (sizeof(int16_t) * 2);

        // Auto-detect which channel holds the microphone data on the first frames
        if (active_channel < 0 && frames_read > 32) {
            int64_t mag_l = 0, mag_r = 0;
            for (int i = 0; i < frames_read; i++) {
                mag_l += abs((int32_t)rx_stereo[i * 2]);
                mag_r += abs((int32_t)rx_stereo[i * 2 + 1]);
            }
            active_channel = (mag_l >= mag_r) ? 0 : 1;
        }

        int ch_offset = (active_channel >= 0) ? active_channel : 0;
        for (int i = 0; i < frames_read; i++) {
            tx_mono[i] = rx_stereo[i * 2 + ch_offset];
        }

        // Stream framed packet to Mac over USB Serial: 0xAA 0x55 <len_lo> <len_hi> <pcm...>
        uint8_t pkt_hdr[4];
        pkt_hdr[0] = 0xAA;
        pkt_hdr[1] = 0x55;
        pkt_hdr[2] = (uint8_t)(frames_read & 0xFF);
        pkt_hdr[3] = (uint8_t)((frames_read >> 8) & 0xFF);
        Serial.write(pkt_hdr, 4);
        Serial.write((const uint8_t *)tx_mono, frames_read * sizeof(int16_t));

        // Save into PSRAM buffer if space available
        if (g_recorded_audio && (g_recorded_count + frames_read <= g_max_record_samples)) {
            memcpy(&g_recorded_audio[g_recorded_count], tx_mono, frames_read * sizeof(int16_t));
            g_recorded_count += frames_read;
        }

        // Button state handling
        if (triggered_by_button) {
            bool btn_down = (digitalRead(BUTTON_BOOT_PIN) == LOW) || (digitalRead(BUTTON_WHEEL_PIN) == LOW);
            uint32_t elapsed = millis() - start_time;

            if (elapsed > 350 && btn_down) {
                // Button held > 350ms: activate Push-to-Talk mode
                is_push_to_talk = true;
            }

            if (is_push_to_talk) {
                // Push-to-Talk: stop immediately when user releases button
                if (!btn_down) {
                    delay(20);
                    if ((digitalRead(BUTTON_BOOT_PIN) == HIGH) && (digitalRead(BUTTON_WHEEL_PIN) == HIGH)) {
                        break;
                    }
                }
            } else {
                // Tap mode:
                if (!btn_down) {
                    button_was_released = true;
                }
                // Second tap stops early
                if (button_was_released && btn_down && elapsed > 400) {
                    delay(30);
                    break;
                }
                // Default tap duration: 5 seconds
                if (elapsed >= 5000) {
                    break;
                }
            }
        }

        // Allow stopping via serial input ('s', 'q', or newline)
        if (Serial.available()) {
            char c = Serial.peek();
            if (c == 's' || c == 'S' || c == 'q' || c == 'Q' || c == '\n' || c == '\r') {
                Serial.read();
                break;
            }
        }
    }

    // Send clean End-of-Stream packet: 0xAA 0x55 0x00 0x00
    uint8_t eos[4] = { 0xAA, 0x55, 0x00, 0x00 };
    Serial.write(eos, 4);
    Serial.flush();

    Serial.printf("\nRECORD_DONE:%u\n", g_recorded_count);

    // Re-enable PA
    if (pa_enabled) {
        digitalWrite(PA_CTRL_PIN, HIGH);
        delay(20);
        // Play quick completion beep
        playTone(800.0f, 40, 0.3f);
    }

    Serial.printf(">>> Audio recording finished (%u samples, %.2f s). Streamed over USB!\n",
                  g_recorded_count, (float)g_recorded_count / SAMPLE_RATE);
}

void playRecordedAudio() {
    if (!g_recorded_audio || g_recorded_count == 0) {
        Serial.println(">>> No recorded audio in buffer to play!");
        return;
    }
    Serial.printf(">>> Playing last recorded audio (%u samples, %.2f s)...\n",
                  g_recorded_count, (float)g_recorded_count / SAMPLE_RATE);

    const int CHUNK = 512;
    int16_t stereo_buf[CHUNK * 2];
    uint32_t samples_played = 0;

    while (samples_played < g_recorded_count) {
        int count = min((uint32_t)CHUNK, (uint32_t)(g_recorded_count - samples_played));
        for (int i = 0; i < count; i++) {
            int16_t val = g_recorded_audio[samples_played + i];
            stereo_buf[i * 2]     = val;
            stereo_buf[i * 2 + 1] = val;
        }
        size_t bytes_written = 0;
        i2s_write(I2S_NUM, stereo_buf, count * sizeof(int16_t) * 2, &bytes_written, portMAX_DELAY);
        samples_played += count;
    }
    Serial.println(">>> Recorded audio playback finished.");
}

void setup() {
    Serial.begin(115200);
    Serial.setRxBufferSize(8192);
    delay(1000); // Allow USB Serial to attach

    Serial.println("\r\n==========================================");
    Serial.println("  ESP32-S3 e-Paper 3.97 Audio & Mic Engine");
    Serial.println("==========================================");

    // Initialize Buttons (Active LOW with internal pull-up resistors)
    pinMode(BUTTON_BOOT_PIN, INPUT_PULLUP);
    pinMode(BUTTON_WHEEL_PIN, INPUT_PULLUP);
    pinMode(BUTTON_UP_PIN, INPUT_PULLUP);
    pinMode(BUTTON_DOWN_PIN, INPUT_PULLUP);
    Serial.println("Buttons initialized (BOOT=GPIO 0, WHEEL=GPIO 5, UP=4, DOWN=6).");

    // Allocate PSRAM Recording Buffer
    g_recorded_audio = (int16_t *)heap_caps_malloc(g_max_record_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!g_recorded_audio) {
        Serial.println("  WARNING: Could not allocate 15s in PSRAM, falling back to internal RAM (5s)");
        g_max_record_samples = SAMPLE_RATE * 5;
        g_recorded_audio = (int16_t *)malloc(g_max_record_samples * sizeof(int16_t));
    }
    if (g_recorded_audio) {
        Serial.printf("  Record buffer allocated: %u samples (%.1f s)\n",
                      g_max_record_samples, (float)g_max_record_samples / SAMPLE_RATE);
    }

    // Initialize I2C
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000);
    scanI2C();

    // Enable Power Amplifier (PA_CTRL = GPIO 39)
    Serial.println("[3/3] Enabling Power Amplifier (GPIO 39 -> HIGH)...");
    pinMode(PA_CTRL_PIN, OUTPUT);
    digitalWrite(PA_CTRL_PIN, HIGH);
    pa_enabled = true;
    delay(50);

    // Initialize Codec & I2S
    if (!initCodec()) {
        Serial.println("CRITICAL: Codec init failed! Check connections.");
        return;
    }
    if (!initI2S()) {
        Serial.println("CRITICAL: I2S init failed!");
        return;
    }

    Serial.println("\nAll audio subsystems initialized successfully!");
    Serial.println("Dumping ES8311 registers:");
    es8311_register_dump(es_handle);
    Serial.println();

    Serial.println("\n==========================================");
    Serial.println("Interactive Controls:");
    Serial.println("  [Hardware Button]: Press BOOT (GPIO 0) or Rotary (GPIO 5) to Record");
    Serial.println("  'R' : Record audio from mic & stream to Mac over USB");
    Serial.println("  'S' : USB Audio Stream mode (play audio streamed from Mac)");
    Serial.println("  '4' : Replay last recorded audio on onboard speaker");
    Serial.println("  '1' : Play diagnostic test beeps");
    Serial.println("  '2' : Play chime melody");
    Serial.println("  '3' : Play onboard music sample track");
    Serial.println("  '+' : Increase speaker volume (+5%)");
    Serial.println("  '-' : Decrease speaker volume (-5%)");
    Serial.println("  'g' : Increase microphone gain (+6dB)");
    Serial.println("  'G' : Decrease microphone gain (-6dB)");
    Serial.println("  'p' : Toggle Power Amplifier (PA_CTRL)");
    Serial.println("  /user <text>  : Post user message on e-Paper (Right bubble)");
    Serial.println("  /agent <text> : Post agent message on e-Paper (Left bubble)");
    Serial.println("  /clear        : Clear e-Paper chat history");
    Serial.println("  /rotate       : Toggle display rotation (90° / 270°)");
    Serial.println("==========================================");

    // Initialize e-Paper Chat Display in vertical mode
    ChatDisplay::begin(ROTATE_270);

    // Play boot melody
    playMelody();
}

static String s_chatSerialBuffer = "";

static void handleAudioCommand(char cmd) {
    switch (cmd) {
        case 's':
        case 'S':
            handleStreamAudio();
            break;
        case 'r':
        case 'R':
            recordAudioSession(false);
            break;
        case '4':
            playRecordedAudio();
            break;
        case '1':
            playBeeps();
            break;
        case '2':
            playMelody();
            break;
        case '3':
            playMusicTrack();
            break;
        case '+':
            current_volume = min(100, current_volume + 5);
            if (es_handle) es8311_voice_volume_set(es_handle, current_volume, NULL);
            Serial.printf("Volume set to %d%%\n", current_volume);
            playTone(1000.0f, 100, 0.5f);
            break;
        case '-':
            current_volume = max(0, current_volume - 5);
            if (es_handle) es8311_voice_volume_set(es_handle, current_volume, NULL);
            Serial.printf("Volume set to %d%%\n", current_volume);
            playTone(600.0f, 100, 0.5f);
            break;
        case 'g':
            current_mic_gain = min((int)ES8311_MIC_GAIN_42DB, current_mic_gain + 1);
            if (es_handle) es8311_microphone_gain_set(es_handle, (es8311_mic_gain_t)current_mic_gain);
            Serial.printf("Mic Gain increased: index %d (+%ddB)\n", current_mic_gain, current_mic_gain * 6);
            playTone(1400.0f, 60, 0.3f);
            break;
        case 'G':
            current_mic_gain = max((int)ES8311_MIC_GAIN_0DB, current_mic_gain - 1);
            if (es_handle) es8311_microphone_gain_set(es_handle, (es8311_mic_gain_t)current_mic_gain);
            Serial.printf("Mic Gain decreased: index %d (+%ddB)\n", current_mic_gain, current_mic_gain * 6);
            playTone(700.0f, 60, 0.3f);
            break;
        case 'p':
        case 'P':
            pa_enabled = !pa_enabled;
            digitalWrite(PA_CTRL_PIN, pa_enabled ? HIGH : LOW);
            Serial.printf("Power Amplifier (PA_CTRL) is now %s\n", pa_enabled ? "ENABLED" : "DISABLED");
            break;
        default:
            break;
    }
}

void loop() {
    // Check hardware buttons: BOOT (GPIO 0) or Rotary Wheel Press (GPIO 5)
    bool btn_boot = (digitalRead(BUTTON_BOOT_PIN) == LOW);
    bool btn_wheel = (digitalRead(BUTTON_WHEEL_PIN) == LOW);
    if (btn_boot || btn_wheel) {
        delay(25); // Debounce
        if ((digitalRead(BUTTON_BOOT_PIN) == LOW) || (digitalRead(BUTTON_WHEEL_PIN) == LOW)) {
            recordAudioSession(true);
            // Wait for button release after session completes
            while ((digitalRead(BUTTON_BOOT_PIN) == LOW) || (digitalRead(BUTTON_WHEEL_PIN) == LOW)) {
                delay(10);
            }
            delay(100);
        }
    }

    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;

        if (c == '\n') {
            s_chatSerialBuffer.trim();
            if (s_chatSerialBuffer.length() > 0) {
                if (s_chatSerialBuffer.length() == 1) {
                    handleAudioCommand(s_chatSerialBuffer[0]);
                } else if (s_chatSerialBuffer.startsWith("/") || s_chatSerialBuffer.startsWith("{") ||
                           s_chatSerialBuffer.startsWith("USER:") || s_chatSerialBuffer.startsWith("user:") ||
                           s_chatSerialBuffer.startsWith("AGENT:") || s_chatSerialBuffer.startsWith("agent:") ||
                           s_chatSerialBuffer.startsWith("u:") || s_chatSerialBuffer.startsWith("a:") ||
                           s_chatSerialBuffer.equalsIgnoreCase("clear") || s_chatSerialBuffer.equalsIgnoreCase("rotate") ||
                           s_chatSerialBuffer.equalsIgnoreCase("demo")) {
                    ChatDisplay::handleCommand(s_chatSerialBuffer);
                    Serial.println("OK");
                    Serial.flush();
                } else {
                    ChatDisplay::addMessage(SENDER_USER, s_chatSerialBuffer.c_str());
                    Serial.println("OK");
                    Serial.flush();
                }
            }
            s_chatSerialBuffer = "";
        } else {
            // Check for instantaneous single keystrokes like 'S' for audio stream
            if (s_chatSerialBuffer.length() == 0 && (c == 's' || c == 'S')) {
                handleStreamAudio();
            } else {
                if (s_chatSerialBuffer.length() < 512) {
                    s_chatSerialBuffer += c;
                }
            }
        }
    }

    delay(10);
}
