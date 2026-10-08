#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "driver/i2s.h"
#include "es8311.h"
#include "music.h"

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

#define SAMPLE_RATE      24000
#define MCLK_MULTIPLE    256
#define MCLK_FREQ_HZ     (SAMPLE_RATE * MCLK_MULTIPLE)

static es8311_handle_t es_handle = NULL;
static int current_volume = 80;
static bool pa_enabled = true;

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
    Serial.printf("  ES8311 initialized successfully. Volume: %d%%\n", current_volume);
    return true;
}

bool initI2S() {
    Serial.println("[2/3] Initializing I2S peripheral...");
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
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
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    err = i2s_set_pin(I2S_NUM, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("  ERROR: i2s_set_pin failed (0x%x)!\n", err);
        return false;
    }

    Serial.println("  I2S driver installed (MCLK=13, BCK=14, WS=47, DOUT=48).");
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

void setup() {
    Serial.begin(115200);
    Serial.setRxBufferSize(8192);
    delay(1000); // Allow USB Serial to attach

    Serial.println("\r\n==========================================");
    Serial.println("  ESP32-S3 e-Paper 3.97 Speaker Test");
    Serial.println("==========================================");

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
    Serial.println("Interactive Commands (type in Serial Monitor):");
    Serial.println("  'S' : USB Audio Stream (stream 24kHz 16-bit mono PCM from Mac)");
    Serial.println("  '1' : Play test beeps");
    Serial.println("  '2' : Play chime melody");
    Serial.println("  '3' : Play onboard music sample track");
    Serial.println("  '+' : Increase volume (+5%)");
    Serial.println("  '-' : Decrease volume (-5%)");
    Serial.println("  'p' : Toggle Power Amplifier (PA_CTRL)");
    Serial.println("==========================================");
}

void loop() {
    if (Serial.available()) {
        char cmd = Serial.read();
        switch (cmd) {
            case 's':
            case 'S':
                handleStreamAudio();
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
    delay(10);
}
