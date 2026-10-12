#include "chat_display.h"
#include <string.h>
#include <algorithm>
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "ChatDisplay";

UBYTE *ChatDisplay::s_frameBuffer = NULL;
uint16_t ChatDisplay::s_rotation = ROTATE_270;
ChatMessage ChatDisplay::s_messages[MAX_CHAT_MESSAGES];
int ChatDisplay::s_messageCount = 0;
bool ChatDisplay::s_initialized = false;

// Bubble typography and layout constants using solely Font24
static const int CHAR_W = 17;        // Font24 width (17 px)
static const int CHAR_H = 24;        // Font24 height (24 px)
static const int LINE_H = 30;        // 24 px char + 6 px line spacing
static const int PAD_X = 14;         // Bubble horizontal inner padding
static const int PAD_Y = 12;         // Bubble vertical inner padding
static const int MAX_BUBBLE_WIDTH = 380; // Max bubble width (leaves 100px opposite margin)
static const int MIN_BUBBLE_WIDTH = 70;  // Min bubble width
static const int MSG_SPACING = 16;   // Gap between consecutive bubbles
static const int TOP_MARGIN = 20;    // Top screen margin
static const int BOTTOM_MARGIN = 20; // Bottom screen margin
static const int CORNER_RADIUS = 10; // Bubble corner roundness

bool ChatDisplay::begin(uint16_t initial_rotation) {
    s_rotation = initial_rotation;
    ESP_LOGI(TAG, "Initializing e-Paper Hardware...");
    DEV_Module_Init();

    UWORD image_size = ((EPD_3IN97_WIDTH % 8 == 0) ? (EPD_3IN97_WIDTH / 8) : (EPD_3IN97_WIDTH / 8 + 1)) * EPD_3IN97_HEIGHT;

    // Allocate frame buffer in PSRAM if available, fallback to internal SRAM
    s_frameBuffer = (UBYTE *)heap_caps_malloc(image_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_frameBuffer) {
        ESP_LOGW(TAG, "PSRAM alloc failed for %u bytes, falling back to internal RAM...", image_size);
        s_frameBuffer = (UBYTE *)malloc(image_size);
    }

    if (!s_frameBuffer) {
        ESP_LOGE(TAG, "CRITICAL ERROR: Could not allocate frame buffer!");
        return false;
    }
    ESP_LOGI(TAG, "Frame buffer allocated (%u bytes).", image_size);

    EPD_3IN97_Init_Fast();

    Paint_NewImage(s_frameBuffer, EPD_3IN97_WIDTH, EPD_3IN97_HEIGHT, s_rotation, WHITE);
    Paint_SetScale(2); // 1-bit B/W

    s_initialized = true;
    refresh();
    ESP_LOGI(TAG, "Initialization complete. Chat UI ready with Font24!");
    return true;
}

void ChatDisplay::clear() {
    s_messageCount = 0;
    ESP_LOGI(TAG, "Chat history cleared.");
    refresh();
}

void ChatDisplay::setRotation(uint16_t rotation) {
    if (rotation != ROTATE_90 && rotation != ROTATE_270) {
        rotation = ROTATE_270;
    }
    s_rotation = rotation;
    if (s_frameBuffer) {
        Paint_NewImage(s_frameBuffer, EPD_3IN97_WIDTH, EPD_3IN97_HEIGHT, s_rotation, WHITE);
        Paint_SetScale(2);
    }
    ESP_LOGI(TAG, "Rotation set to %d deg.", s_rotation);
    refresh();
}

void ChatDisplay::toggleRotation() {
    setRotation(s_rotation == ROTATE_270 ? ROTATE_90 : ROTATE_270);
}

uint16_t ChatDisplay::getRotation() {
    return s_rotation;
}

int ChatDisplay::getMessageCount() {
    return s_messageCount;
}

bool ChatDisplay::addMessage(ChatSender sender, const char *text, const char *timeStr) {
    if (!text || strlen(text) == 0) return false;

    // Shift left if message buffer is full
    if (s_messageCount >= MAX_CHAT_MESSAGES) {
        for (int i = 0; i < MAX_CHAT_MESSAGES - 1; i++) {
            s_messages[i] = s_messages[i + 1];
        }
        s_messageCount = MAX_CHAT_MESSAGES - 1;
    }

    ChatMessage &m = s_messages[s_messageCount];
    m.sender = sender;
    strncpy(m.text, text, sizeof(m.text) - 1);
    m.text[sizeof(m.text) - 1] = '\0';

    if (timeStr && strlen(timeStr) > 0) {
        strncpy(m.timeStr, timeStr, sizeof(m.timeStr) - 1);
        m.timeStr[sizeof(m.timeStr) - 1] = '\0';
    } else {
        uint64_t sec = esp_timer_get_time() / 1000000ULL;
        unsigned int mm = (unsigned int)((sec / 60) % 60);
        unsigned int ss = (unsigned int)(sec % 60);
        snprintf(m.timeStr, sizeof(m.timeStr), "%02u:%02u", mm, ss);
    }

    s_messageCount++;
    ESP_LOGI(TAG, "Added %s message #%d: \"%s\"",
             sender == SENDER_USER ? "USER" : "AGENT", s_messageCount, m.text);

    refresh();
    return true;
}

void ChatDisplay::drawTextRaw(int x, int y, const char *str, sFONT *font, uint16_t fg_color, uint16_t bg_color, bool transparent_bg) {
    if (!str || !font) return;
    int cur_x = x;
    int cur_y = y;

    while (*str) {
        char c = *str++;
        if (c == '\n') {
            cur_y += font->Height + 6;
            cur_x = x;
            continue;
        }
        if (c < ' ' || c > '~') c = ' ';

        uint32_t char_offset = (uint32_t)(c - ' ') * font->Height * (font->Width / 8 + (font->Width % 8 ? 1 : 0));
        const unsigned char *ptr = &font->table[char_offset];

        for (int page = 0; page < font->Height; page++) {
            for (int col = 0; col < font->Width; col++) {
                if (*ptr & (0x80 >> (col % 8))) {
                    Paint_SetPixel(cur_x + col, cur_y + page, fg_color);
                } else if (!transparent_bg) {
                    Paint_SetPixel(cur_x + col, cur_y + page, bg_color);
                }
                if (col % 8 == 7) ptr++;
            }
            if (font->Width % 8 != 0) ptr++;
        }
        cur_x += font->Width;
    }
}

static void drawQuarterArc(int cx, int cy, int r, int quadrant, uint16_t color, DOT_PIXEL line_width) {
    int x = 0;
    int y = r;
    int d = 3 - 2 * r;

    while (x <= y) {
        if (quadrant == 1) {
            Paint_DrawPoint(cx + x, cy - y, color, line_width, DOT_STYLE_DFT);
            Paint_DrawPoint(cx + y, cy - x, color, line_width, DOT_STYLE_DFT);
        } else if (quadrant == 2) {
            Paint_DrawPoint(cx - x, cy - y, color, line_width, DOT_STYLE_DFT);
            Paint_DrawPoint(cx - y, cy - x, color, line_width, DOT_STYLE_DFT);
        } else if (quadrant == 3) {
            Paint_DrawPoint(cx - x, cy + y, color, line_width, DOT_STYLE_DFT);
            Paint_DrawPoint(cx - y, cy + x, color, line_width, DOT_STYLE_DFT);
        } else if (quadrant == 4) {
            Paint_DrawPoint(cx + x, cy + y, color, line_width, DOT_STYLE_DFT);
            Paint_DrawPoint(cx + y, cy + x, color, line_width, DOT_STYLE_DFT);
        }

        if (d < 0) {
            d += 4 * x + 6;
        } else {
            d += 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
}

void ChatDisplay::drawRoundedRect(int x0, int y0, int x1, int y1, int r, uint16_t color, bool filled) {
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    if (r < 0) r = 0;
    if (r > (x1 - x0) / 2) r = (x1 - x0) / 2;
    if (r > (y1 - y0) / 2) r = (y1 - y0) / 2;

    if (r == 0) {
        Paint_DrawRectangle(x0, y0, x1, y1, color, DOT_PIXEL_2X2, filled ? DRAW_FILL_FULL : DRAW_FILL_EMPTY);
        return;
    }

    if (filled) {
        Paint_DrawRectangle(x0, y0 + r, x1, y1 - r, color, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawRectangle(x0 + r, y0, x1 - r, y0 + r, color, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawRectangle(x0 + r, y1 - r, x1 - r, y1, color, DOT_PIXEL_1X1, DRAW_FILL_FULL);

        int x = 0;
        int y = r;
        int d = 3 - 2 * r;
        while (x <= y) {
            Paint_DrawLine(x0 + r - y, y0 + r - x, x0 + r, y0 + r - x, color, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
            Paint_DrawLine(x1 - r, y0 + r - x, x1 - r + y, y0 + r - x, color, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
            Paint_DrawLine(x0 + r - x, y0 + r - y, x0 + r, y0 + r - y, color, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
            Paint_DrawLine(x1 - r, y0 + r - y, x1 - r + x, y0 + r - y, color, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

            Paint_DrawLine(x0 + r - y, y1 - r + x, x0 + r, y1 - r + x, color, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
            Paint_DrawLine(x1 - r, y1 - r + x, x1 - r + y, y1 - r + x, color, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
            Paint_DrawLine(x0 + r - x, y1 - r + y, x0 + r, y1 - r + y, color, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
            Paint_DrawLine(x1 - r, y1 - r + y, x1 - r + x, y1 - r + y, color, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

            if (d < 0) {
                d += 4 * x + 6;
            } else {
                d += 4 * (x - y) + 10;
                y--;
            }
            x++;
        }
    } else {
        Paint_DrawLine(x0 + r, y0, x1 - r, y0, color, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
        Paint_DrawLine(x0 + r, y1, x1 - r, y1, color, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
        Paint_DrawLine(x0, y0 + r, x0, y1 - r, color, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
        Paint_DrawLine(x1, y0 + r, x1, y1 - r, color, DOT_PIXEL_2X2, LINE_STYLE_SOLID);

        drawQuarterArc(x1 - r, y0 + r, r, 1, color, DOT_PIXEL_2X2);
        drawQuarterArc(x0 + r, y0 + r, r, 2, color, DOT_PIXEL_2X2);
        drawQuarterArc(x0 + r, y1 - r, r, 3, color, DOT_PIXEL_2X2);
        drawQuarterArc(x1 - r, y1 - r, r, 4, color, DOT_PIXEL_2X2);
    }
}

static void wrapMessageText(const char *text, int max_chars_per_line, char lines[][32], int &line_count, int &max_len) {
    line_count = 0;
    max_len = 0;
    if (!text || strlen(text) == 0) return;

    char current_line[32] = "";
    int cur_len = 0;

    const char *p = text;
    while (*p && line_count < 14) {
        char word[32] = "";
        int wlen = 0;
        while (*p && *p != ' ' && *p != '\n' && wlen < max_chars_per_line) {
            word[wlen++] = *p++;
        }
        word[wlen] = '\0';

        bool is_newline = (*p == '\n');
        if (*p == ' ' || *p == '\n') p++;

        if (wlen > 0) {
            if (cur_len == 0) {
                strncpy(current_line, word, sizeof(current_line) - 1);
                cur_len = wlen;
            } else if (cur_len + 1 + wlen <= max_chars_per_line) {
                strncat(current_line, " ", sizeof(current_line) - strlen(current_line) - 1);
                strncat(current_line, word, sizeof(current_line) - strlen(current_line) - 1);
                cur_len += 1 + wlen;
            } else {
                strncpy(lines[line_count], current_line, 31);
                lines[line_count][31] = '\0';
                if (cur_len > max_len) max_len = cur_len;
                line_count++;

                strncpy(current_line, word, sizeof(current_line) - 1);
                cur_len = wlen;
            }
        }

        if (is_newline && cur_len > 0 && line_count < 14) {
            strncpy(lines[line_count], current_line, 31);
            lines[line_count][31] = '\0';
            if (cur_len > max_len) max_len = cur_len;
            line_count++;
            current_line[0] = '\0';
            cur_len = 0;
        }
    }

    if (cur_len > 0 && line_count < 14) {
        strncpy(lines[line_count], current_line, 31);
        lines[line_count][31] = '\0';
        if (cur_len > max_len) max_len = cur_len;
        line_count++;
    }
}

void ChatDisplay::renderMessage(int x, int y, int bubble_w, int bubble_h, const ChatMessage &msg, const char lines[][32], int line_count) {
    if (msg.sender == SENDER_USER) {
        drawRoundedRect(x, y, x + bubble_w, y + bubble_h, CORNER_RADIUS, BLACK, true);

        int tail_base_x = x + bubble_w;
        int tail_tip_y = y + bubble_h - 4;
        for (int dy = 0; dy <= 6; dy++) {
            Paint_DrawLine(tail_base_x, y + bubble_h - 14 + dy,
                           tail_base_x + dy, tail_tip_y, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        }

        int text_y = y + PAD_Y;
        for (int i = 0; i < line_count; i++) {
            drawTextRaw(x + PAD_X, text_y, lines[i], &Font24, WHITE, BLACK, true);
            text_y += LINE_H;
        }
    } else {
        Paint_DrawRectangle(x, y, x + bubble_w, y + bubble_h, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        drawRoundedRect(x, y, x + bubble_w, y + bubble_h, CORNER_RADIUS, BLACK, false);

        int tail_base_x = x;
        int tail_tip_y = y + bubble_h - 4;
        for (int dy = 0; dy <= 6; dy++) {
            Paint_DrawLine(tail_base_x, y + bubble_h - 14 + dy,
                           tail_base_x - dy, tail_tip_y, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        }

        int text_y = y + PAD_Y;
        for (int i = 0; i < line_count; i++) {
            drawTextRaw(x + PAD_X, text_y, lines[i], &Font24, BLACK, WHITE, false);
            text_y += LINE_H;
        }
    }
}

void ChatDisplay::refresh() {
    if (!s_initialized || !s_frameBuffer) return;

    ESP_LOGI(TAG, "Rendering display canvas...");
    Paint_Clear(WHITE);

    if (s_messageCount > 0) {
        struct MsgLayout {
            int bubble_w;
            int bubble_h;
            int total_h;
        };
        MsgLayout layouts[MAX_CHAT_MESSAGES];

        int max_chars = (MAX_BUBBLE_WIDTH - PAD_X * 2) / CHAR_W;

        char tmp_lines[14][32];
        int tmp_count = 0, tmp_len = 0;
        for (int i = 0; i < s_messageCount; i++) {
            wrapMessageText(s_messages[i].text, max_chars, tmp_lines, tmp_count, tmp_len);
            int bw = tmp_len * CHAR_W + PAD_X * 2;
            if (bw < MIN_BUBBLE_WIDTH) bw = MIN_BUBBLE_WIDTH;
            if (bw > MAX_BUBBLE_WIDTH) bw = MAX_BUBBLE_WIDTH;
            layouts[i].bubble_w = bw;

            int bh = (tmp_count > 0 ? (tmp_count - 1) * LINE_H + CHAR_H : CHAR_H) + PAD_Y * 2;
            layouts[i].bubble_h = bh;
            layouts[i].total_h = bh + MSG_SPACING;
        }

        const int AVAILABLE_H = Paint.Height - TOP_MARGIN - BOTTOM_MARGIN;

        int start_idx = 0;
        int running_h = 0;
        for (int i = s_messageCount - 1; i >= 0; i--) {
            if (running_h + layouts[i].total_h <= AVAILABLE_H) {
                running_h += layouts[i].total_h;
                start_idx = i;
            } else {
                break;
            }
        }

        int cur_y = TOP_MARGIN;
        for (int i = start_idx; i < s_messageCount; i++) {
            wrapMessageText(s_messages[i].text, max_chars, tmp_lines, tmp_count, tmp_len);
            int bw = layouts[i].bubble_w;
            int bh = layouts[i].bubble_h;
            int bx = (s_messages[i].sender == SENDER_USER) ? (Paint.Width - 16 - bw) : 16;

            renderMessage(bx, cur_y, bw, bh, s_messages[i], tmp_lines, tmp_count);
            cur_y += layouts[i].total_h;
        }
    }

    ESP_LOGI(TAG, "Refreshing e-Paper panel (Fast Base Mode)...");
    EPD_3IN97_Display_Fast_Base(s_frameBuffer);
    ESP_LOGI(TAG, "Screen refresh complete!");
}

static std::string trimString(const std::string &str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

static bool startsWithCaseInsensitive(const std::string &str, const std::string &prefix) {
    if (str.length() < prefix.length()) return false;
    for (size_t i = 0; i < prefix.length(); i++) {
        if (tolower((unsigned char)str[i]) != tolower((unsigned char)prefix[i])) return false;
    }
    return true;
}

static bool equalsCaseInsensitive(const std::string &a, const std::string &b) {
    if (a.length() != b.length()) return false;
    for (size_t i = 0; i < a.length(); i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    }
    return true;
}

bool ChatDisplay::handleCommand(const char *cmd_cstr) {
    if (!cmd_cstr) return false;
    return handleCommand(std::string(cmd_cstr));
}

bool ChatDisplay::handleCommand(const std::string &raw_cmd) {
    std::string cmd = trimString(raw_cmd);
    if (cmd.empty()) return false;

    // JSON payload detection
    if (cmd.front() == '{' && cmd.back() == '}') {
        ChatSender sender = SENDER_USER;
        std::string text = "";
        std::string time_val = "";

        if (cmd.find("\"type\":\"agent\"") != std::string::npos ||
            cmd.find("\"role\":\"agent\"") != std::string::npos ||
            cmd.find("\"role\":\"assistant\"") != std::string::npos) {
            sender = SENDER_AGENT;
        }

        size_t text_idx = cmd.find("\"text\":");
        if (text_idx != std::string::npos) {
            size_t q1 = cmd.find("\"", text_idx + 7);
            if (q1 != std::string::npos) {
                size_t q2 = cmd.find("\"", q1 + 1);
                if (q2 != std::string::npos && q2 > q1) {
                    text = cmd.substr(q1 + 1, q2 - q1 - 1);
                }
            }
        }

        size_t time_idx = cmd.find("\"time\":");
        if (time_idx != std::string::npos) {
            size_t t1 = cmd.find("\"", time_idx + 7);
            if (t1 != std::string::npos) {
                size_t t2 = cmd.find("\"", t1 + 1);
                if (t2 != std::string::npos && t2 > t1) {
                    time_val = cmd.substr(t1 + 1, t2 - t1 - 1);
                }
            }
        }

        if (!text.empty()) {
            return addMessage(sender, text.c_str(), !time_val.empty() ? time_val.c_str() : NULL);
        }
    }

    if (equalsCaseInsensitive(cmd, "/clear") || equalsCaseInsensitive(cmd, "CLEAR")) {
        clear();
        return true;
    }

    if (equalsCaseInsensitive(cmd, "/rotate") || equalsCaseInsensitive(cmd, "ROTATE")) {
        toggleRotation();
        return true;
    }

    if (startsWithCaseInsensitive(cmd, "/rotate 90") || startsWithCaseInsensitive(cmd, "/orientation 90")) {
        setRotation(ROTATE_90);
        return true;
    }

    if (startsWithCaseInsensitive(cmd, "/rotate 270") || startsWithCaseInsensitive(cmd, "/orientation 270")) {
        setRotation(ROTATE_270);
        return true;
    }

    if (equalsCaseInsensitive(cmd, "/demo") || equalsCaseInsensitive(cmd, "DEMO")) {
        clear();
        addMessage(SENDER_USER, "Hello! How are you?", "10:41");
        addMessage(SENDER_AGENT, "Hi! I am your AI assistant running on ESP32.", "10:41");
        addMessage(SENDER_USER, "Audio and Chat display work together!", "10:42");
        addMessage(SENDER_AGENT, "Yes, ALSA audio and e-Paper chat over one USB cable.", "10:42");
        return true;
    }

    // User message prefixes
    if (startsWithCaseInsensitive(cmd, "/user ")) {
        return addMessage(SENDER_USER, cmd.substr(6).c_str());
    }
    if (startsWithCaseInsensitive(cmd, "USER:")) {
        return addMessage(SENDER_USER, cmd.substr(5).c_str());
    }
    if (startsWithCaseInsensitive(cmd, "u:")) {
        return addMessage(SENDER_USER, cmd.substr(2).c_str());
    }

    // Agent message prefixes
    if (startsWithCaseInsensitive(cmd, "/agent ")) {
        return addMessage(SENDER_AGENT, cmd.substr(7).c_str());
    }
    if (startsWithCaseInsensitive(cmd, "AGENT:")) {
        return addMessage(SENDER_AGENT, cmd.substr(6).c_str());
    }
    if (startsWithCaseInsensitive(cmd, "a:")) {
        return addMessage(SENDER_AGENT, cmd.substr(2).c_str());
    }

    return false;
}
