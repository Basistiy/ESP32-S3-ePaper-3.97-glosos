/*
 * Waveshare ESP32-S3 e-Paper 3.97" Native USB Audio Class (UAC) Firmware
 *
 * Implements a full-duplex ALSA Sound Card over USB (Microphone + Speaker)
 * powered by ES8311 I2S Audio Codec & NS4150B Class-D Power Amplifier.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "usb_device_uac.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_vol.h"
#include "esp_log.h"

static const char *TAG = "audio_uac";

/* Hardware pin assignments from Kconfig / sdkconfig */
#define I2C_NUM                 (0)
#define I2C_SCL_PIN             CONFIG_EXAMPLE_I2C_SCL_IO
#define I2C_SDA_PIN             CONFIG_EXAMPLE_I2C_SDA_IO

#define I2S_NUM                 (0)
#define I2S_MCK_PIN             CONFIG_EXAMPLE_I2S_MCK_IO
#define I2S_BCK_PIN             CONFIG_EXAMPLE_I2S_BCK_IO
#define I2S_WS_PIN              CONFIG_EXAMPLE_I2S_WS_IO
#define I2S_DOUT_PIN            CONFIG_EXAMPLE_I2S_DO_IO
#define I2S_DIN_PIN             CONFIG_EXAMPLE_I2S_DI_IO
#define PA_CTRL_PIN             CONFIG_EXAMPLE_PA_CTRL_IO

/* Audio stream parameters */
#define AUDIO_SAMPLE_RATE       CONFIG_UAC_SAMPLE_RATE
#define AUDIO_BIT_WIDTH         CONFIG_EXAMPLE_AUDIO_BIT_WIDTH
#define CODEC_MCLK_MULTIPLE     (256)
#define CODEC_MCLK_FREQ_HZ      (CODEC_MCLK_MULTIPLE * AUDIO_SAMPLE_RATE)
#define DEFAULT_VOLUME          CONFIG_EXAMPLE_CODEC_VOLUME
#define DEFAULT_MIC_GAIN        CONFIG_EXAMPLE_CODEC_MIC_GAIN

static i2s_chan_handle_t s_i2s_tx_handle = NULL;
static i2s_chan_handle_t s_i2s_rx_handle = NULL;
static i2c_master_bus_handle_t s_i2c_bus_handle = NULL;
static esp_codec_dev_handle_t s_codec_handle = NULL;

/* -------------------------------------------------------------------------
 * Hardware Power Amplifier (NS4150B) Control
 * ------------------------------------------------------------------------- */
static void pa_power_enable(bool enable)
{
    if (PA_CTRL_PIN >= 0) {
        gpio_set_level((gpio_num_t)PA_CTRL_PIN, enable ? 1 : 0);
        ESP_LOGD(TAG, "PA (GPIO %d) set to %s", PA_CTRL_PIN, enable ? "HIGH (ON)" : "LOW (OFF)");
    }
}

static void pa_gpio_init(void)
{
    if (PA_CTRL_PIN >= 0) {
        gpio_config_t pa_cfg = {
            .pin_bit_mask = (1ULL << PA_CTRL_PIN),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&pa_cfg);
        pa_power_enable(true);
        vTaskDelay(pdMS_TO_TICKS(20));
        ESP_LOGI(TAG, "Onboard Power Amplifier initialized on GPIO %d (Active HIGH)", PA_CTRL_PIN);
    }
}

/* -------------------------------------------------------------------------
 * I2S Master Bus Initialization (Duplex: TX + RX)
 * ------------------------------------------------------------------------- */
static void i2s_driver_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 128;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_i2s_tx_handle, &s_i2s_rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(AUDIO_BIT_WIDTH, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_MCK_PIN,
            .bclk = I2S_BCK_PIN,
            .ws = I2S_WS_PIN,
            .dout = I2S_DOUT_PIN,
            .din = I2S_DIN_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    /* Output audio on both Left & Right slots for mono DAC */
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_i2s_rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_i2s_tx_handle, &std_cfg));

    ESP_ERROR_CHECK(i2s_channel_enable(s_i2s_rx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(s_i2s_tx_handle));

    ESP_LOGI(TAG, "I2S initialized at %d Hz (MCLK=%d, BCK=%d, WS=%d, DOUT=%d, DIN=%d)",
             AUDIO_SAMPLE_RATE, I2S_MCK_PIN, I2S_BCK_PIN, I2S_WS_PIN, I2S_DOUT_PIN, I2S_DIN_PIN);
}

/* -------------------------------------------------------------------------
 * I2C Master Bus Initialization (ES8311 Codec Control)
 * ------------------------------------------------------------------------- */
static void i2c_driver_init(void)
{
    i2c_master_bus_config_t i2c_mst_cfg = {
        .i2c_port = I2C_NUM,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_cfg, &s_i2c_bus_handle));
    ESP_LOGI(TAG, "I2C initialized (SDA=%d, SCL=%d)", I2C_SDA_PIN, I2C_SCL_PIN);
}

/* -------------------------------------------------------------------------
 * ES8311 Codec Initialization via esp_codec_dev
 * ------------------------------------------------------------------------- */
static void codec_es8311_init(void)
{
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus_handle,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(ctrl_if);

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM,
        .rx_handle = s_i2s_rx_handle,
        .tx_handle = s_i2s_tx_handle,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    assert(data_if);

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    assert(gpio_if);

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .master_mode = false,
        .use_mclk = true,
        .pa_pin = PA_CTRL_PIN,
        .pa_reverted = false,
        .hw_gain = {
            .pa_voltage = 5.0,
            .codec_dac_voltage = 3.3,
        },
        .mclk_div = CODEC_MCLK_MULTIPLE,
    };
    const audio_codec_if_t *es8311_if = es8311_codec_new(&es8311_cfg);
    assert(es8311_if);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = es8311_if,
        .data_if = data_if,
    };
    s_codec_handle = esp_codec_dev_new(&dev_cfg);
    assert(s_codec_handle);

    esp_codec_dev_sample_info_t sample_cfg = {
        .bits_per_sample = AUDIO_BIT_WIDTH,
        .channel = 1,
        .channel_mask = 0x01,
        .sample_rate = AUDIO_SAMPLE_RATE,
    };
    ESP_ERROR_CHECK(esp_codec_dev_open(s_codec_handle, &sample_cfg));

    /* Unmute, set volume and mic pre-amp gain */
    esp_codec_dev_set_out_mute(s_codec_handle, false);
    esp_codec_dev_set_out_vol(s_codec_handle, DEFAULT_VOLUME);
    esp_codec_dev_set_in_gain(s_codec_handle, DEFAULT_MIC_GAIN);

    ESP_LOGI(TAG, "ES8311 Codec configured (Volume: %d%%, Mic Gain: +%d dB)",
             DEFAULT_VOLUME, DEFAULT_MIC_GAIN);
}

/* -------------------------------------------------------------------------
 * Diagnostic Boot Chime (C5 - E5 - G5)
 * ------------------------------------------------------------------------- */
#if CONFIG_EXAMPLE_PLAY_BOOT_CHIME
static void play_tone(float freq_hz, int duration_ms, float volume)
{
    int total_samples = (AUDIO_SAMPLE_RATE * duration_ms) / 1000;
    const int CHUNK = 128;
    int16_t buffer[CHUNK];
    float phase = 0.0f;
    float phase_step = (2.0f * (float)M_PI * freq_hz) / (float)AUDIO_SAMPLE_RATE;

    int samples_sent = 0;
    while (samples_sent < total_samples) {
        int count = (total_samples - samples_sent > CHUNK) ? CHUNK : (total_samples - samples_sent);
        for (int i = 0; i < count; i++) {
            float env = 1.0f;
            int idx = samples_sent + i;
            if (idx < 80) env = (float)idx / 80.0f;
            else if (total_samples - idx < 80) env = (float)(total_samples - idx) / 80.0f;

            buffer[i] = (int16_t)(sinf(phase) * 28000.0f * volume * env);
            phase += phase_step;
            if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
        }
        esp_codec_dev_write(s_codec_handle, buffer, count * sizeof(int16_t));
        samples_sent += count;
    }
}

static void play_boot_chime(void)
{
    ESP_LOGI(TAG, "Playing startup confirmation chime...");
    play_tone(523.25f, 90, 0.4f);  // C5
    vTaskDelay(pdMS_TO_TICKS(15));
    play_tone(659.25f, 90, 0.4f);  // E5
    vTaskDelay(pdMS_TO_TICKS(15));
    play_tone(783.99f, 150, 0.5f); // G5
    ESP_LOGI(TAG, "Startup chime finished - Speaker is fully functional!");
}
#endif

/* -------------------------------------------------------------------------
 * USB Audio Class (UAC) Stream & Mixer Callbacks
 * ------------------------------------------------------------------------- */
static esp_err_t uac_output_cb(uint8_t *buf, size_t len, void *arg)
{
    /* Host -> ESP32 Speaker */
    int ret = esp_codec_dev_write(s_codec_handle, buf, len);
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t uac_input_cb(uint8_t *buf, size_t len, size_t *bytes_read, void *arg)
{
    /* ESP32 Mic -> Host */
    int ret = esp_codec_dev_read(s_codec_handle, buf, len);
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    if (bytes_read) {
        *bytes_read = len;
    }
    return ESP_OK;
}

static void uac_set_mute_cb(uint32_t mute, void *arg)
{
    esp_codec_dev_set_out_mute(s_codec_handle, !!mute);
    pa_power_enable(!mute);
    ESP_LOGI(TAG, "[ALSA Mixer] Speaker %s", mute ? "MUTED" : "UNMUTED");
}

static void uac_set_volume_cb(uint32_t volume, void *arg)
{
    if (volume > 100) volume = 100;
    esp_codec_dev_set_out_vol(s_codec_handle, volume);
    ESP_LOGI(TAG, "[ALSA Mixer] Volume set to: %"PRIu32"%%", volume);
}

static void usb_uac_init(void)
{
    uac_device_config_t config = {
        .output_cb = uac_output_cb,
        .input_cb = uac_input_cb,
        .set_mute_cb = uac_set_mute_cb,
        .set_volume_cb = uac_set_volume_cb,
        .cb_ctx = NULL,
    };
    ESP_ERROR_CHECK(uac_device_init(&config));
    ESP_LOGI(TAG, "USB Audio Class device registered (ALSA Playback & Capture ready)");
}

/* -------------------------------------------------------------------------
 * Application Entry Point
 * ------------------------------------------------------------------------- */
void app_main(void)
{
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  Waveshare ESP32-S3 e-Paper 3.97 ALSA Audio Engine");
    ESP_LOGI(TAG, "  Rate: %d Hz | ES8311 Codec | NS4150B PA", AUDIO_SAMPLE_RATE);
    ESP_LOGI(TAG, "==================================================");

    /* 1. Hardware Power Amplifier */
    pa_gpio_init();

    /* 2. I2S Peripheral (Audio TX & RX) */
    i2s_driver_init();

    /* 3. I2C Peripheral (Codec Control) */
    i2c_driver_init();

    /* 4. ES8311 Codec Driver */
    codec_es8311_init();

    /* 5. Play startup test chime */
#if CONFIG_EXAMPLE_PLAY_BOOT_CHIME
    play_boot_chime();
#endif

    /* 6. Register USB Audio Class Device */
    usb_uac_init();

    ESP_LOGI(TAG, "Audio subsystem operational. Ready for ALSA streaming over USB!");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
