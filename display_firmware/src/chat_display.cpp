#include "chat_display.h"

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
    Serial.println("[ChatDisplay] Initializing e-Paper Hardware...");
    DEV_Module_Init();

    UWORD image_size = ((EPD_3IN97_WIDTH % 8 == 0) ? (EPD_3IN97_WIDTH / 8) : (EPD_3IN97_WIDTH / 8 + 1)) * EPD_3IN97_HEIGHT;
    
    // Allocate frame buffer in PSRAM if available, fallback to internal SRAM
    s_frameBuffer = (UBYTE *)heap_caps_malloc(image_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_frameBuffer) {
        Serial.printf("[ChatDisplay] PSRAM alloc failed for %u bytes, falling back to internal RAM...\n", image_size);
        s_frameBuffer = (UBYTE *)malloc(image_size);
    }

    if (!s_frameBuffer) {
        Serial.println("[ChatDisplay] CRITICAL ERROR: Could not allocate frame buffer!");
        return false;
    }
    Serial.printf("[ChatDisplay] Frame buffer allocated (%u bytes).\n", image_size);

    EPD_3IN97_Init_Fast();

    Paint_NewImage(s_frameBuffer, EPD_3IN97_WIDTH, EPD_3IN97_HEIGHT, s_rotation, WHITE);
    Paint_SetScale(2); // 1-bit B/W

    s_initialized = true;
    refresh();
    Serial.println("[ChatDisplay] Initialization complete. Pure Chat UI ready with Font24!");
    return true;
}

void ChatDisplay::clear() {
    s_messageCount = 0;
    Serial.println("[ChatDisplay] Chat history cleared.");
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
    Serial.printf("[ChatDisplay] Rotation set to %d deg.\n", s_rotation);
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
        unsigned long sec = millis() / 1000;
        unsigned int mm = (sec / 60) % 60;
        unsigned int ss = sec % 60;
        snprintf(m.timeStr, sizeof(m.timeStr), "%02u:%02u", mm, ss);
    }

    s_messageCount++;
    Serial.printf("[ChatDisplay] Added %s message #%d: \"%s\"\n",
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
    // quadrant 1: Top-Right (x >= cx, y <= cy)
    // quadrant 2: Top-Left  (x <= cx, y <= cy)
    // quadrant 3: Bottom-Left (x <= cx, y >= cy)
    // quadrant 4: Bottom-Right (x >= cx, y >= cy)
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
        // Main vertical center body
        Paint_DrawRectangle(x0, y0 + r, x1, y1 - r, color, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        // Top and bottom horizontal slabs between corners
        Paint_DrawRectangle(x0 + r, y0, x1 - r, y0 + r, color, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawRectangle(x0 + r, y1 - r, x1 - r, y1, color, DOT_PIXEL_1X1, DRAW_FILL_FULL);

        // Fill corner quarter-arcs with scanlines (strictly inside corner quadrants)
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
        // Outline: straight edges between corners
        Paint_DrawLine(x0 + r, y0, x1 - r, y0, color, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
        Paint_DrawLine(x0 + r, y1, x1 - r, y1, color, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
        Paint_DrawLine(x0, y0 + r, x0, y1 - r, color, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
        Paint_DrawLine(x1, y0 + r, x1, y1 - r, color, DOT_PIXEL_2X2, LINE_STYLE_SOLID);

        // 4 corner arcs: strictly 90-degree outer quadrants, ZERO circles inside!
        drawQuarterArc(x1 - r, y0 + r, r, 1, color, DOT_PIXEL_2X2); // Top-Right
        drawQuarterArc(x0 + r, y0 + r, r, 2, color, DOT_PIXEL_2X2); // Top-Left
        drawQuarterArc(x0 + r, y1 - r, r, 3, color, DOT_PIXEL_2X2); // Bottom-Left
        drawQuarterArc(x1 - r, y1 - r, r, 4, color, DOT_PIXEL_2X2); // Bottom-Right
    }
}

// Splits message text into word-wrapped lines (up to max_chars_per_line)
static void wrapMessageText(const char *text, int max_chars_per_line, char lines[][32], int &line_count, int &max_len) {
    line_count = 0;
    max_len = 0;
    if (!text || strlen(text) == 0) return;

    char current_line[32] = "";
    int cur_len = 0;

    const char *p = text;
    while (*p && line_count < 14) {
        // Collect next word chunk (respecting max_chars_per_line)
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
        // --- USER MESSAGE (RIGHT SIDE, SOLID BLACK BUBBLE, WHITE TEXT) ---
        drawRoundedRect(x, y, x + bubble_w, y + bubble_h, CORNER_RADIUS, BLACK, true);

        // Right-pointing speech tail (solid black triangle)
        int tail_base_x = x + bubble_w;
        int tail_tip_y = y + bubble_h - 4;
        for (int dy = 0; dy <= 6; dy++) {
            Paint_DrawLine(tail_base_x, y + bubble_h - 14 + dy,
                           tail_base_x + dy, tail_tip_y, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        }

        // White Text on Black Bubble
        int text_y = y + PAD_Y;
        for (int i = 0; i < line_count; i++) {
            drawTextRaw(x + PAD_X, text_y, lines[i], &Font24, WHITE, BLACK, true);
            text_y += LINE_H;
        }
    } else {
        // --- AGENT MESSAGE (LEFT SIDE, WHITE BUBBLE WITH 2PX BLACK OUTLINE, BLACK TEXT) ---
        Paint_DrawRectangle(x, y, x + bubble_w, y + bubble_h, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        drawRoundedRect(x, y, x + bubble_w, y + bubble_h, CORNER_RADIUS, BLACK, false);

        // Left-pointing speech tail (black outline triangle)
        int tail_base_x = x;
        int tail_tip_y = y + bubble_h - 4;
        for (int dy = 0; dy <= 6; dy++) {
            Paint_DrawLine(tail_base_x, y + bubble_h - 14 + dy,
                           tail_base_x - dy, tail_tip_y, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        }

        // Black Text on White Bubble
        int text_y = y + PAD_Y;
        for (int i = 0; i < line_count; i++) {
            drawTextRaw(x + PAD_X, text_y, lines[i], &Font24, BLACK, WHITE, false);
            text_y += LINE_H;
        }
    }
}

void ChatDisplay::refresh() {
    if (!s_initialized || !s_frameBuffer) return;

    Serial.println("[ChatDisplay] Rendering display canvas...");
    Paint_Clear(WHITE);

    // Only chat bubbles - no header bars, no footer status lines
    if (s_messageCount > 0) {
        struct MsgLayout {
            int bubble_w;
            int bubble_h;
            int total_h;
        };
        MsgLayout layouts[MAX_CHAT_MESSAGES];

        int max_chars = (MAX_BUBBLE_WIDTH - PAD_X * 2) / CHAR_W; // (380 - 28) / 17 = 20 chars/line

        // Pass 1: Measure dimensions of all messages
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

        // Determine which messages fit within visible chat canvas
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

        // Pass 2: Render only visible messages
        int cur_y = TOP_MARGIN;
        for (int i = start_idx; i < s_messageCount; i++) {
            wrapMessageText(s_messages[i].text, max_chars, tmp_lines, tmp_count, tmp_len);
            int bw = layouts[i].bubble_w;
            int bh = layouts[i].bubble_h;
            int bx = 0;

            if (s_messages[i].sender == SENDER_USER) {
                // User message on RIGHT side (16px margin from right edge)
                bx = Paint.Width - 16 - bw;
            } else {
                // Agent message on LEFT side (16px margin from left edge)
                bx = 16;
            }

            renderMessage(bx, cur_y, bw, bh, s_messages[i], tmp_lines, tmp_count);
            cur_y += layouts[i].total_h;
        }
    }

    Serial.println("[ChatDisplay] Refreshing e-Paper panel (Fast Base Mode)...");
    EPD_3IN97_Display_Fast_Base(s_frameBuffer);
    Serial.println("[ChatDisplay] Screen refresh complete!");
}

bool ChatDisplay::handleCommand(const String &raw_cmd) {
    String cmd = raw_cmd;
    cmd.trim();
    if (cmd.length() == 0) return false;

    // Check JSON message: e.g. {"type":"user","text":"Hello"}
    if (cmd.startsWith("{")) {
        ChatSender sender = SENDER_USER;
        String text = "";
        String time_val = "";

        if (cmd.indexOf("\"cmd\":\"clear\"") >= 0 || cmd.indexOf("\"clear\"") >= 0) {
            clear();
            return true;
        }
        if (cmd.indexOf("\"cmd\":\"rotate\"") >= 0) {
            toggleRotation();
            return true;
        }

        if (cmd.indexOf("\"type\":\"agent\"") >= 0 || cmd.indexOf("\"role\":\"agent\"") >= 0 || cmd.indexOf("\"role\":\"assistant\"") >= 0) {
            sender = SENDER_AGENT;
        }

        int text_idx = cmd.indexOf("\"text\":");
        if (text_idx >= 0) {
            int q1 = cmd.indexOf("\"", text_idx + 7);
            if (q1 >= 0) {
                int q2 = cmd.indexOf("\"", q1 + 1);
                if (q2 > q1) {
                    text = cmd.substring(q1 + 1, q2);
                }
            }
        }

        int time_idx = cmd.indexOf("\"time\":");
        if (time_idx >= 0) {
            int t1 = cmd.indexOf("\"", time_idx + 7);
            if (t1 >= 0) {
                int t2 = cmd.indexOf("\"", t1 + 1);
                if (t2 > t1) {
                    time_val = cmd.substring(t1 + 1, t2);
                }
            }
        }

        if (text.length() > 0) {
            return addMessage(sender, text.c_str(), time_val.length() > 0 ? time_val.c_str() : NULL);
        }
    }

    // Command prefix checks
    if (cmd.equalsIgnoreCase("/clear") || cmd.equalsIgnoreCase("CLEAR")) {
        clear();
        return true;
    }

    if (cmd.equalsIgnoreCase("/rotate") || cmd.equalsIgnoreCase("ROTATE")) {
        toggleRotation();
        return true;
    }

    if (cmd.startsWith("/rotate 90") || cmd.startsWith("/orientation 90")) {
        setRotation(ROTATE_90);
        return true;
    }

    if (cmd.startsWith("/rotate 270") || cmd.startsWith("/orientation 270")) {
        setRotation(ROTATE_270);
        return true;
    }

    if (cmd.equalsIgnoreCase("/demo") || cmd.equalsIgnoreCase("DEMO")) {
        clear();
        addMessage(SENDER_USER, "Hello! How are you?", "10:41");
        addMessage(SENDER_AGENT, "Hi! I am your AI assistant running on ESP32.", "10:41");
        addMessage(SENDER_USER, "Font24 looks so clean!", "10:42");
        addMessage(SENDER_AGENT, "Yes, large crisp bubbles with no borders or headers.", "10:42");
        return true;
    }

    // User message prefixes: "/user <msg>", "USER:<msg>", "u:<msg>"
    if (cmd.startsWith("/user ") || cmd.startsWith("/USER ")) {
        return addMessage(SENDER_USER, cmd.substring(6).c_str());
    }
    if (cmd.startsWith("USER:") || cmd.startsWith("user:")) {
        return addMessage(SENDER_USER, cmd.substring(5).c_str());
    }
    if (cmd.startsWith("u:") || cmd.startsWith("U:")) {
        return addMessage(SENDER_USER, cmd.substring(2).c_str());
    }

    // Agent message prefixes: "/agent <msg>", "AGENT:<msg>", "a:<msg>"
    if (cmd.startsWith("/agent ") || cmd.startsWith("/AGENT ")) {
        return addMessage(SENDER_AGENT, cmd.substring(7).c_str());
    }
    if (cmd.startsWith("AGENT:") || cmd.startsWith("agent:")) {
        return addMessage(SENDER_AGENT, cmd.substring(6).c_str());
    }
    if (cmd.startsWith("a:") || cmd.startsWith("A:")) {
        return addMessage(SENDER_AGENT, cmd.substring(2).c_str());
    }

    return false;
}
