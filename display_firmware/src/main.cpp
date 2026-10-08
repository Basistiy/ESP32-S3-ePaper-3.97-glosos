#include <Arduino.h>
#include "DEV_Config.h"
#include "EPD_3in97.h"
#include "image_data.h"

// Configuration: Change to 0 for 1-bit B/W, 1 for 4-Level Grayscale
#define USE_4GRAY 1

// Configuration: Change to 0 for Fit Entire (with margins), 1 for Prominent Crop
#define USE_PROMINENT 1

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\r\n========================================");
    Serial.println("  Waveshare ESP32-S3 e-Paper 3.97 Display");
    Serial.println("========================================");

    Serial.println("[1/4] Initializing hardware GPIO & SPI...");
    DEV_Module_Init();

#if USE_4GRAY
    Serial.println("[2/4] Initializing 4-Level Grayscale mode...");
    EPD_3IN97_Init_4GRAY();

    Serial.println("[3/4] Sending image data to e-Paper...");
#if USE_PROMINENT
    EPD_3IN97_Display_4Gray(gImage_prominent_4gray);
#else
    EPD_3IN97_Display_4Gray(gImage_fit_4gray);
#endif

#else
    Serial.println("[2/4] Initializing Fast 1-Bit B/W mode...");
    EPD_3IN97_Init_Fast();

    Serial.println("[3/4] Sending image data to e-Paper...");
#if USE_PROMINENT
    EPD_3IN97_Display(gImage_prominent_1bit);
#else
    EPD_3IN97_Display(gImage_fit_1bit);
#endif
#endif

    Serial.println("[4/4] Refresh complete. Putting display into deep sleep...");
    EPD_3IN97_Sleep();
    DEV_Delay_ms(2000);
    DEV_Module_Exit();

    Serial.println("\n[SUCCESS] Image is permanently latched onto the e-Paper!");
    Serial.println("You can safely disconnect or power off the board.");
}

void loop() {
    delay(10000);
}
