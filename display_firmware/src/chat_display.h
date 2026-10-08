#ifndef _CHAT_DISPLAY_H_
#define _CHAT_DISPLAY_H_

#include <Arduino.h>
#include "DEV_Config.h"
#include "EPD_3in97.h"
#include "GUI_Paint.h"
#include "fonts.h"

enum ChatSender {
    SENDER_USER,
    SENDER_AGENT
};

struct ChatMessage {
    ChatSender sender;
    char text[384];
    char timeStr[16];
};

#define MAX_CHAT_MESSAGES 16

class ChatDisplay {
public:
    static bool begin(uint16_t initial_rotation = ROTATE_270);
    static void clear();
    static bool addMessage(ChatSender sender, const char *text, const char *timeStr = NULL);
    static void setRotation(uint16_t rotation);
    static void toggleRotation();
    static uint16_t getRotation();
    static int getMessageCount();
    
    // Renders chat bubbles and performs e-Paper fast refresh
    static void refresh();

    // Parse and handle serial command line (e.g. "/user hello", "/agent hi", "/clear", etc.)
    static bool handleCommand(const String &cmd);

private:
    static UBYTE *s_frameBuffer;
    static uint16_t s_rotation;
    static ChatMessage s_messages[MAX_CHAT_MESSAGES];
    static int s_messageCount;
    static bool s_initialized;

    // Drawing helpers
    static void drawRoundedRect(int x0, int y0, int x1, int y1, int r, uint16_t color, bool filled);
    static void drawTextRaw(int x, int y, const char *str, sFONT *font, uint16_t fg_color, uint16_t bg_color, bool transparent_bg);
    static void renderMessage(int x, int y, int bubble_w, int bubble_h, const ChatMessage &msg, const char lines[][32], int line_count);
};

#endif // _CHAT_DISPLAY_H_
