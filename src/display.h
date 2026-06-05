#pragma once
#include <stdint.h>

// Top of the touch zone — used by main.cpp for hit testing
static const int DISP_TOUCH_Y = 205;

void display_init();

// 0–50px: state bar coloured by pipeline state
void display_status(const char* label, uint32_t bg_color);

// 50–175px: AI response panel (large, prominent)
void display_response(const char* text);

// 175–205px: transcript panel (compact, shows what you said)
void display_transcript(const char* text);

// Clear both conversation panels
void display_clear_conversation();
