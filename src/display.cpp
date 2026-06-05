#include "display.h"
#include <M5Unified.h>
#include <string.h>

static const int SCR_W = 320;

// Zone boundaries
static const int STATUS_Y = 0;   static const int STATUS_H = 50;
static const int RESP_Y   = 50;  static const int RESP_H   = 125;  // AI response — big
static const int TRANS_Y  = 175; static const int TRANS_H  = 30;   // You transcript — compact
// DISP_TOUCH_Y = 205, touch zone to 240 — defined in display.h

static const uint32_t COL_RESP_BG  = 0x0420;  // very dark green
static const uint32_t COL_TRANS_BG = 0x0841;  // very dark blue-grey
static const uint32_t COL_LABEL    = 0x8410;  // mid-grey label text

// Replace common multi-byte UTF-8 punctuation with ASCII equivalents so
// drawChar (single-byte) doesn't render each byte as a separate glyph.
static void sanitize_utf8(const char* src, char* dst, size_t dst_size) {
    size_t out = 0;
    const unsigned char* p = (const unsigned char*)src;
    while (*p && out < dst_size - 1) {
        // 3-byte UTF-8 sequences starting with E2 80 xx (General Punctuation block)
        if (p[0] == 0xE2 && p[1] == 0x80) {
            char rep = '\'';
            switch (p[2]) {
                case 0x98: case 0x99: rep = '\''; break;  // ' '
                case 0x9C: case 0x9D: rep = '"';  break;  // " "
                case 0x93: case 0x94: rep = '-';  break;  // – —
                case 0xA6:            rep = '.';  break;  // … (just use '.')
                default:              rep = ' ';  break;
            }
            dst[out++] = rep;
            p += 3;
            continue;
        }
        // Skip any other non-ASCII byte (drop rather than show garbage)
        if (p[0] > 0x7F) { p++; continue; }
        dst[out++] = (char)*p++;
    }
    dst[out] = '\0';
}

// Word-wrap text into a panel. font_size 1 = 6×9px, font_size 2 = 12×18px.
// Truncates with "..." if text overflows the panel.
static void draw_wrapped(const char* text, int x, int y, int w, int h,
                         uint32_t bg, uint32_t fg, int font_size) {
    M5.Display.fillRect(x, y, w, h, bg);
    if (!text || !text[0]) return;

    const int fw = font_size == 2 ? 12 : 6;
    const int fh = font_size == 2 ? 18 : 9;
    int cols = w / fw;
    int rows = h / fh;

    M5.Display.setTextSize(font_size);
    M5.Display.setTextColor(fg, bg);

    int cx = x + 2, cy = y + 2;
    int row = 0;
    const char* p = text;

    // Ring buffer tracking last 3 drawn character positions for "..." placement
    int tail_x[3] = {x + 2, x + 2, x + 2};
    int tail_y[3] = {y + 2, y + 2, y + 2};
    int tail_i = 0;

    while (*p && row < rows) {
        const char* word_start = p;
        while (*p && *p != ' ' && *p != '\n') p++;
        int wlen = p - word_start;

        int cur_col = (cx - (x + 2)) / fw;
        if (cur_col > 0 && cur_col + wlen > cols) {
            cx = x + 2;
            cy += fh;
            row++;
            if (row >= rows) break;
        }

        for (int i = 0; i < wlen && row < rows; i++) {
            tail_x[tail_i % 3] = cx;
            tail_y[tail_i % 3] = cy;
            tail_i++;
            M5.Display.drawChar(word_start[i], cx, cy);
            cx += fw;
            if ((cx - (x + 2)) / fw >= cols) {
                cx = x + 2;
                cy += fh;
                row++;
            }
        }

        if (*p == '\n')     { cx = x + 2; cy += fh; row++; p++; }
        else if (*p == ' ') { cx += fw; p++; }
    }

    // If text was clipped, overwrite last 3 chars with "..."
    if (*p) {
        for (int i = 0; i < 3; i++) {
            int ti = (tail_i + i) % 3;
            M5.Display.fillRect(tail_x[ti], tail_y[ti], fw, fh, bg);
            M5.Display.drawChar('.', tail_x[ti], tail_y[ti]);
        }
    }
}

static void draw_label(const char* label, int y, int h, uint32_t bg) {
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(COL_LABEL, bg);
    M5.Display.setCursor(3, y + 2);
    M5.Display.print(label);
    M5.Display.drawFastHLine(0, y + 11, SCR_W, 0x2104);
}

void display_init() {
    M5.Display.fillScreen(TFT_BLACK);
    display_status("INIT", 0x2104);
    display_clear_conversation();
}

void display_status(const char* label, uint32_t bg_color) {
    M5.Display.fillRect(0, STATUS_Y, SCR_W, STATUS_H, bg_color);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(TFT_WHITE, bg_color);
    int lw = M5.Display.textWidth(label);
    M5.Display.setCursor((SCR_W - lw) / 2, (STATUS_H - 24) / 2);
    M5.Display.print(label);
    M5.Display.drawFastHLine(0, STATUS_Y + STATUS_H, SCR_W, 0x4208);
}

void display_response(const char* text) {
    char safe[512];
    sanitize_utf8(text, safe, sizeof(safe));
    M5.Display.fillRect(0, RESP_Y, SCR_W, RESP_H, COL_RESP_BG);
    draw_label("AI:", RESP_Y, RESP_H, COL_RESP_BG);
    draw_wrapped(safe, 3, RESP_Y + 14, SCR_W - 6, RESP_H - 16, COL_RESP_BG, TFT_WHITE, 2);
    M5.Display.drawFastHLine(0, RESP_Y + RESP_H, SCR_W, 0x4208);
}

void display_transcript(const char* text) {
    char safe[256];
    sanitize_utf8(text, safe, sizeof(safe));
    M5.Display.fillRect(0, TRANS_Y, SCR_W, TRANS_H, COL_TRANS_BG);
    draw_label("You:", TRANS_Y, TRANS_H, COL_TRANS_BG);
    draw_wrapped(safe, 3, TRANS_Y + 14, SCR_W - 6, TRANS_H - 16, COL_TRANS_BG, TFT_WHITE, 1);
    M5.Display.drawFastHLine(0, TRANS_Y + TRANS_H, SCR_W, 0x4208);
}

void display_clear_conversation() {
    display_response("");
    display_transcript("");
}
