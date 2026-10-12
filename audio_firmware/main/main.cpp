/*
 * Waveshare ESP32-S3 e-Paper 3.97" Unified Composite Firmware
 *
 * Implements:
 * 1. Native USB Audio Class (UAC / ALSA) Sound Card (Speaker + Microphone)
 * 2. USB CDC Serial Port for real-time e-Paper chat messages and control
 * 3. 3.97" e-Paper Display driver with vertical chat bubbles layout (Font24)
 * 4. Hardware Power Amplifier (NS4150B) on GPIO 39 & ES8311 I2S Audio Codec
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "tusb.h"
#include "usb_device_uac.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_vol.h"
#include "esp_log.h"
#include "chat_display.h"

static const char *TAG = "unified_fw";

/* Hardware pin assignments from Kconfig / sdkconfig */
#define I2C_NUM                 (0)
#define I2C_SCL_PIN             CONFIG_EXAMPLE_I2C_SCL_IO
#define I2C_SDA_PIN             CONFIG_EXAMPLE_I2C_SDA_IO

#define I2S_NUM                 ((i2s_port_t)0)
#define I2S_MCK_PIN             CONFIG_EXAMPLE_I2S_MCK_IO
#define I2S_BCK_PIN             CONFIG_EXAMPLE_I2S_BCK_IO
#define I2S_WS_PIN              CONFIG_EXAMPLE_I2S_WS_IO
#define I2S_DOUT_PIN            CONFIG_EXAMPLE_I2S_DO_IO
#define I2S_DIN_PIN             CONFIG_EXAMPLE_I2S_DI_IO
#define PA_CTRL_PIN             CONFIG_EXAMPLE_PA_CTRL_IO

#define BUTTON_BOOT_PIN         (0)
#define BUTTON_WHEEL_PIN        (5)
#define BUTTON_UP_PIN           (4)
#define BUTTON_DOWN_PIN         (6)

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
        ESP_LOGI(TAG, "Power Amplifier initialized on GPIO %d (Active HIGH)", PA_CTRL_PIN);
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
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG((i2s_data_bit_width_t)AUDIO_BIT_WIDTH, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = (gpio_num_t)I2S_MCK_PIN,
            .bclk = (gpio_num_t)I2S_BCK_PIN,
            .ws = (gpio_num_t)I2S_WS_PIN,
            .dout = (gpio_num_t)I2S_DOUT_PIN,
            .din = (gpio_num_t)I2S_DIN_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_i2s_tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_i2s_rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_i2s_tx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(s_i2s_rx_handle));

    ESP_LOGI(TAG, "I2S initialized (MCLK:%d, BCK:%d, WS:%d, DOUT:%d, DIN:%d, Rate:%d Hz)",
             I2S_MCK_PIN, I2S_BCK_PIN, I2S_WS_PIN, I2S_DOUT_PIN, I2S_DIN_PIN, AUDIO_SAMPLE_RATE);
}

/* -------------------------------------------------------------------------
 * I2C Master Bus Initialization (Codec Control)
 * ------------------------------------------------------------------------- */
static void i2c_driver_init(void)
{
    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port = (i2c_port_num_t)I2C_NUM,
        .sda_io_num = (gpio_num_t)I2C_SDA_PIN,
        .scl_io_num = (gpio_num_t)I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &s_i2c_bus_handle));
    ESP_LOGI(TAG, "I2C master bus initialized (SDA:%d, SCL:%d)", I2C_SDA_PIN, I2C_SCL_PIN);
}

/* -------------------------------------------------------------------------
 * ES8311 Codec Initialization
 * ------------------------------------------------------------------------- */
static void codec_es8311_init(void)
{
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = (i2c_port_num_t)I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus_handle,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = (i2s_port_t)I2S_NUM,
        .rx_handle = s_i2s_rx_handle,
        .tx_handle = s_i2s_tx_handle,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = PA_CTRL_PIN,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = {
            .pa_voltage = 5.0,
            .codec_dac_voltage = 3.3,
        },
        .no_dac_ref = false,
        .mclk_div = (uint16_t)CODEC_MCLK_MULTIPLE,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_cfg);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_codec_handle = esp_codec_dev_new(&dev_cfg);
    assert(s_codec_handle != NULL);

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = (uint8_t)AUDIO_BIT_WIDTH,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = (uint32_t)AUDIO_SAMPLE_RATE,
        .mclk_multiple = (uint16_t)CODEC_MCLK_MULTIPLE,
    };
    ESP_ERROR_CHECK(esp_codec_dev_open(s_codec_handle, &fs));
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(s_codec_handle, DEFAULT_VOLUME));
    ESP_ERROR_CHECK(esp_codec_dev_set_in_gain(s_codec_handle, DEFAULT_MIC_GAIN));
    ESP_ERROR_CHECK(esp_codec_dev_set_out_mute(s_codec_handle, false));

    // Force hardware registers to known good state matching Waveshare reference driver
    esp_codec_dev_write_reg(s_codec_handle, 0x00, 0x80); // Power on chip in slave mode
    esp_codec_dev_write_reg(s_codec_handle, 0x0D, 0x01); // Power up analog circuitry
    esp_codec_dev_write_reg(s_codec_handle, 0x0E, 0x02); // Enable analog PGA and ADC modulator
    esp_codec_dev_write_reg(s_codec_handle, 0x12, 0x00); // Power up DAC
    esp_codec_dev_write_reg(s_codec_handle, 0x13, 0x10); // Enable output to HP/speaker drive
    esp_codec_dev_write_reg(s_codec_handle, 0x31, 0x00); // Digital unmute DAC
    esp_codec_dev_write_reg(s_codec_handle, 0x32, 0xBF); // DAC digital volume (~75%)
    esp_codec_dev_write_reg(s_codec_handle, 0x37, 0x08); // Bypass DAC equalizer

    // Ensure NS4150B power amplifier is enabled
    pa_power_enable(true);

    ESP_LOGI(TAG, "ES8311 codec configured (Volume: %d%%, Mic Gain: %d dB)", DEFAULT_VOLUME, DEFAULT_MIC_GAIN);
}

/* -------------------------------------------------------------------------
 * Diagnostic Boot Chime Melody (C5 - E5 - G5 - C6)
 * ------------------------------------------------------------------------- */
static void play_tone(float freq_hz, int duration_ms, float volume)
{
    int num_samples = (int)(AUDIO_SAMPLE_RATE * (duration_ms / 1000.0f));
    int16_t *buf = (int16_t *)malloc(num_samples * sizeof(int16_t));
    if (!buf) return;

    for (int i = 0; i < num_samples; i++) {
        float t = (float)i / AUDIO_SAMPLE_RATE;
        float fade = 1.0f;
        if (i < 80) fade = (float)i / 80.0f;
        else if (i > num_samples - 80) fade = (float)(num_samples - i) / 80.0f;
        buf[i] = (int16_t)(sinf(2.0f * (float)M_PI * freq_hz * t) * 32767.0f * volume * fade);
    }

    esp_codec_dev_write(s_codec_handle, buf, num_samples * sizeof(int16_t));
    free(buf);
}

static void play_boot_chime(void)
{
    ESP_LOGI(TAG, "Playing hardware startup chime...");
    play_tone(523.25f, 120, 0.60f); // C5
    vTaskDelay(pdMS_TO_TICKS(20));
    play_tone(659.25f, 120, 0.60f); // E5
    vTaskDelay(pdMS_TO_TICKS(20));
    play_tone(783.99f, 120, 0.60f); // G5
    vTaskDelay(pdMS_TO_TICKS(20));
    play_tone(1046.50f, 250, 0.70f); // C6
    vTaskDelay(pdMS_TO_TICKS(50));
}

/* -------------------------------------------------------------------------
 * UAC Audio Callbacks
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
    ESP_LOGI(TAG, "[ALSA Mixer] Volume set to: %" PRIu32 "%%", volume);
}

static void usb_uac_init(void)
{
    uac_device_config_t config = {
        .skip_tinyusb_init = false,
        .output_cb = uac_output_cb,
        .input_cb = uac_input_cb,
        .set_mute_cb = uac_set_mute_cb,
        .set_volume_cb = uac_set_volume_cb,
        .cb_ctx = NULL,
#if CONFIG_USB_DEVICE_UAC_AS_PART
        .spk_itf_num = 3,
        .mic_itf_num = 4,
#endif
    };
    ESP_ERROR_CHECK(uac_device_init(&config));
    ESP_LOGI(TAG, "Composite USB stack initialized (CDC + UAC Audio)");
}

/* -------------------------------------------------------------------------
 * Thread-Safe CDC Serial Sender
 * ------------------------------------------------------------------------- */
static SemaphoreHandle_t s_cdc_tx_mutex = NULL;

static void cdc_send_line(const char *line)
{
    if (!s_cdc_tx_mutex || !line) return;
    if (xSemaphoreTake(s_cdc_tx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        tud_cdc_n_write(0, line, strlen(line));
        tud_cdc_n_write_flush(0);
        xSemaphoreGive(s_cdc_tx_mutex);
    }
}

/* -------------------------------------------------------------------------
 * CDC Chat Serial Task (receives /user, /agent, /clear, /rotate, /demo, JSON)
 * ------------------------------------------------------------------------- */
static void chat_cdc_task(void *arg)
{
    static char rx_buf[1024];
    static size_t rx_len = 0;

    ESP_LOGI(TAG, "Chat CDC serial listener ready.");

    while (1) {
        if (tud_cdc_n_available(0)) {
            uint32_t count = tud_cdc_n_read(0, rx_buf + rx_len, sizeof(rx_buf) - rx_len - 1);
            if (count > 0) {
                rx_len += count;
                rx_buf[rx_len] = '\0';

                char *newline_pos = strchr(rx_buf, '\n');
                while (newline_pos) {
                    *newline_pos = '\0';
                    size_t line_len = strlen(rx_buf);
                    if (line_len > 0 && rx_buf[line_len - 1] == '\r') {
                        rx_buf[line_len - 1] = '\0';
                    }

                    if (strlen(rx_buf) > 0) {
                        ESP_LOGI("CHAT", "Received command: \"%s\"", rx_buf);
                        if (strcasecmp(rx_buf, "/beep") == 0 || strcasecmp(rx_buf, "/testaudio") == 0) {
                            play_tone(880.0f, 300, 0.60f);
                            cdc_send_line("BEEP OK\r\n");
                        } else {
                            bool handled = ChatDisplay::handleCommand(rx_buf);
                            if (!handled) {
                                ChatDisplay::addMessage(SENDER_USER, rx_buf);
                            }
                            cdc_send_line("OK\r\n");
                        }
                    }

                    size_t processed = (newline_pos - rx_buf) + 1;
                    memmove(rx_buf, newline_pos + 1, rx_len - processed);
                    rx_len -= processed;
                    rx_buf[rx_len] = '\0';
                    newline_pos = strchr(rx_buf, '\n');
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* -------------------------------------------------------------------------
 * Hardware Button Tracking & USB Event Transmission
 * ------------------------------------------------------------------------- */
typedef struct {
    gpio_num_t pin;
    const char *id;
    bool is_pressed;
    bool last_raw_pressed;
    uint32_t debounce_timer;
    uint32_t press_start_ms;
} button_input_t;

static button_input_t s_buttons[] = {
    { (gpio_num_t)BUTTON_BOOT_PIN,  "boot",        false, false, 0, 0 },
    { (gpio_num_t)BUTTON_WHEEL_PIN, "wheel_click", false, false, 0, 0 },
    { (gpio_num_t)BUTTON_UP_PIN,    "wheel_up",    false, false, 0, 0 },
    { (gpio_num_t)BUTTON_DOWN_PIN,  "wheel_down",  false, false, 0, 0 }
};

static void button_task(void *arg)
{
    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); i++) {
        gpio_config_t btn_cfg = {
            .pin_bit_mask = (1ULL << s_buttons[i].pin),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&btn_cfg);
    }

    // Allow internal pull-ups to stabilize
    vTaskDelay(pdMS_TO_TICKS(50));

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); i++) {
        bool raw_pressed = (gpio_get_level(s_buttons[i].pin) == 0);
        s_buttons[i].is_pressed = raw_pressed;
        s_buttons[i].last_raw_pressed = raw_pressed;
        s_buttons[i].debounce_timer = now;
        s_buttons[i].press_start_ms = now;
    }

    const uint32_t DEBOUNCE_MS = 25;
    char msg[128];

    while (1) {
        now = (uint32_t)(esp_timer_get_time() / 1000ULL);

        for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); i++) {
            button_input_t *b = &s_buttons[i];
            bool raw_pressed = (gpio_get_level(b->pin) == 0);

            if (raw_pressed != b->last_raw_pressed) {
                b->last_raw_pressed = raw_pressed;
                b->debounce_timer = now;
            }

            if ((now - b->debounce_timer) >= DEBOUNCE_MS) {
                if (raw_pressed && !b->is_pressed) {
                    // Transition: RELEASED -> PRESSED
                    b->is_pressed = true;
                    b->press_start_ms = now;
                    ESP_LOGI(TAG, "Button '%s' PRESSED", b->id);
                    snprintf(msg, sizeof(msg), "{\"event\":\"button\",\"id\":\"%s\",\"state\":\"press\"}\r\n", b->id);
                    cdc_send_line(msg);
                } else if (!raw_pressed && b->is_pressed) {
                    // Transition: PRESSED -> RELEASED
                    b->is_pressed = false;
                    uint32_t duration_ms = now - b->press_start_ms;
                    ESP_LOGI(TAG, "Button '%s' RELEASED (duration: %u ms)", b->id, (unsigned)duration_ms);
                    snprintf(msg, sizeof(msg), "{\"event\":\"button\",\"id\":\"%s\",\"state\":\"release\",\"duration_ms\":%u}\r\n",
                             b->id, (unsigned)duration_ms);
                    cdc_send_line(msg);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* -------------------------------------------------------------------------
 * Application Entry Point
 * ------------------------------------------------------------------------- */
extern "C" uint8_t const *tud_descriptor_device_cb(void);
extern "C" uint8_t const *tud_descriptor_configuration_cb(uint8_t index);
extern "C" uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid);

extern "C" void app_main(void)
{
    // Force inclusion of usb_descriptors.o from archive
    volatile void *dummy_usb_refs[] = {
        (void *)tud_descriptor_device_cb,
        (void *)tud_descriptor_configuration_cb,
        (void *)tud_descriptor_string_cb,
    };
    (void)dummy_usb_refs;

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

    /* 6. Initialize 3.97" e-Paper Chat Display in Portrait mode */
    if (!ChatDisplay::begin(ROTATE_270)) {
        ESP_LOGE(TAG, "Failed to initialize e-Paper Chat Display!");
    }

    /* 7. Register Composite USB Stack (CDC Serial + UAC Audio) */
    usb_uac_init();

    /* 8. Initialize CDC Mutex for thread-safe transmission */
    s_cdc_tx_mutex = xSemaphoreCreateMutex();

    /* 9. Start Background Tasks on Core 0 */
    xTaskCreatePinnedToCore(chat_cdc_task, "chat_cdc", 12288, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(button_task, "button_task", 8192, NULL, 2, NULL, 0);

    ESP_LOGI(TAG, "Unified system operational: ALSA audio + e-Paper chat ready!");
}
