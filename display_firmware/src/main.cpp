#include <Arduino.h>
#include "chat_display.h"

// Hardware buttons
#define BUTTON_BOOT_PIN   0   // Onboard BOOT button
#define BUTTON_WHEEL_PIN  5   // Onboard Rotary Wheel Center

static String s_serialBuffer = "";

void setup() {
    Serial.begin(115200);
    Serial.setRxBufferSize(4096);
    delay(1000);

    Serial.println("\r\n================================================");
    Serial.println("  Waveshare ESP32-S3 e-Paper 3.97 Chat Terminal");
    Serial.println("  Vertical Chat Bubbles Display System");
    Serial.println("================================================");
    Serial.flush();

    pinMode(BUTTON_BOOT_PIN, INPUT_PULLUP);
    pinMode(BUTTON_WHEEL_PIN, INPUT_PULLUP);

    // Initialize Chat Display in Vertical (Portrait) orientation
    if (!ChatDisplay::begin(ROTATE_270)) {
        Serial.println("[ERROR] Failed to initialize Chat Display!");
        while (1) { delay(1000); }
    }

    Serial.println("\n[READY] Listening for messages over USB Serial (115200 baud).");
    Serial.println("Commands:");
    Serial.println("  /user <text>       - Post user message (Right side, black bubble)");
    Serial.println("  /agent <text>      - Post agent message (Left side, white bubble)");
    Serial.println("  /clear             - Clear chat history");
    Serial.println("  /rotate            - Toggle 180° rotation (90° / 270°)");
    Serial.println("  /demo              - Run multi-bubble demo");
    Serial.println("  (Or send JSON: {\"type\":\"user\",\"text\":\"...\"})");
    Serial.println("  [Hardware]: Press BOOT button to cycle rotation.");
    Serial.println("------------------------------------------------\n");
}

void loop() {
    // Check BOOT button (GPIO 0)
    if (digitalRead(BUTTON_BOOT_PIN) == LOW) {
        delay(30);
        if (digitalRead(BUTTON_BOOT_PIN) == LOW) {
            Serial.println("[BUTTON] BOOT pressed -> Toggling orientation...");
            ChatDisplay::toggleRotation();
            while (digitalRead(BUTTON_BOOT_PIN) == LOW) {
                delay(20);
            }
            delay(100);
        }
    }

    // Process serial data
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;

        if (c == '\n') {
            s_serialBuffer.trim();
            if (s_serialBuffer.length() > 0) {
                Serial.printf("[INPUT] Received: \"%s\"\n", s_serialBuffer.c_str());
                bool handled = ChatDisplay::handleCommand(s_serialBuffer);
                if (handled) {
                    Serial.println("OK");
                } else {
                    // Default fallback: if no prefix, treat as user message
                    ChatDisplay::addMessage(SENDER_USER, s_serialBuffer.c_str());
                    Serial.println("OK");
                }
                Serial.flush();
            }
            s_serialBuffer = "";
        } else {
            if (s_serialBuffer.length() < 512) {
                s_serialBuffer += c;
            }
        }
    }

    delay(10);
}
