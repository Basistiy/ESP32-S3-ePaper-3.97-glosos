/*
 * Composite USB Descriptors (CDC Serial + UAC Audio)
 * For ESP32-S3 e-Paper 3.97" Board
 */

#include <string.h>
#include "tusb.h"
#include "uac_descriptors.h"
#include "esp_log.h"

static const char *TAG = "USB_DESC";

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,

    // Composite device with IAD (Interface Association Descriptor)
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = 0x303A,  // Espressif VID
    .idProduct          = 0x8000,  // Composite PID
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+
enum {
    ITF_NUM_CDC_CONTROL = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_AUDIO_CONTROL,
    ITF_NUM_AUDIO_STREAMING_SPK,
    ITF_NUM_AUDIO_STREAMING_MIC,
    ITF_NUM_TOTAL
};

#define EPNUM_AUDIO_OUT   0x01
#define EPNUM_AUDIO_FB    0x81
#define EPNUM_AUDIO_IN    0x82

#define EPNUM_CDC_NOTIF   0x83
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x84

#define CDC_NOTIF_SIZE    64
#define CDC_DATA_SIZE     64

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_AUDIO_DEVICE_DESC_LEN)

uint8_t const desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 200),

    // CDC: Interface 0 (Control) & Interface 1 (Data)
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_CONTROL, 4, EPNUM_CDC_NOTIF, CDC_NOTIF_SIZE, EPNUM_CDC_OUT, EPNUM_CDC_IN, CDC_DATA_SIZE),

    // Audio: Interface 2 (Control), Interface 3 (Speaker), Interface 4 (Mic)
    TUD_AUDIO_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, 5, EPNUM_AUDIO_OUT, EPNUM_AUDIO_IN, EPNUM_AUDIO_FB),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+
char const *string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 },    // 0: Supported language (English 0x0409)
    "Waveshare",                      // 1: Manufacturer
    "ESP32-S3 Audio & Chat Display",  // 2: Product
    "12345678",                       // 3: Serial
    "Chat Serial Port",               // 4: CDC Interface
    "Audio Control",                  // 5: Audio Control Interface
    "Speaker",                        // 6: Speaker Interface
    "Microphone",                     // 7: Microphone Interface
};

static uint16_t _desc_str[64];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    uint8_t chr_count;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) {
            return NULL;
        }

        const char *str = string_desc_arr[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 63) {
            chr_count = 63;
        }

        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}

// Invoked when device is mounted
void tud_mount_cb(void)
{
    ESP_LOGI(TAG, "USB composite device mounted");
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    ESP_LOGI(TAG, "USB composite device unmounted");
}

// Invoked when usb bus is suspended
void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    ESP_LOGI(TAG, "USB composite device suspended");
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
    ESP_LOGI(TAG, "USB composite device resumed");
}
