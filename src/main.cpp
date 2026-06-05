#include <M5Unified.h>
#include <WiFi.h>
#include "audio_capture.h"
#include "audio_playback.h"
#include "secrets.h"
#include "stt.h"
#include "llm.h"
#include "tts.h"
#include "display.h"
#include "config.h"

static const uint32_t REC_MAX_MS = 8000;

// Status bar colors (RGB565)
static const uint32_t COL_IDLE      = 0x0010;
static const uint32_t COL_RECORDING = 0xA000;
static const uint32_t COL_STT       = 0x8400;
static const uint32_t COL_LLM       = 0x4010;
static const uint32_t COL_SPEAKING  = 0x0400;

enum State { IDLE, RECORDING, STT_PENDING, LLM_PENDING, SPEAKING };
static State    state    = IDLE;
static uint32_t state_ms = 0;

static char transcript[512];
static char llm_response[1024];

static void draw_touch_bar(bool active) {
    uint32_t col = active ? 0xA000 : 0x2104;
    M5.Lcd.fillRect(0, DISP_TOUCH_Y, 320, 240 - DISP_TOUCH_Y, col);
    M5.Lcd.setTextColor(TFT_WHITE);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(60, DISP_TOUCH_Y + 10);
    M5.Lcd.print(active ? "  RECORDING...  " : "  HOLD TO SPEAK ");
}

static void wifi_connect() {
    display_status("WIFI", COL_IDLE);
    WiFi.begin(g_secrets.wifi_ssid, g_secrets.wifi_pass);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 30) {
        delay(500);
        tries++;
    }
    if (WiFi.status() != WL_CONNECTED) {
        display_transcript("WiFi FAILED — check secrets");
    }
}

static void enter_idle() {
    state    = IDLE;
    state_ms = millis();
    display_status("READY", COL_IDLE);
    draw_touch_bar(false);
}

static void enter_recording() {
    audio_capture_start();
    state    = RECORDING;
    state_ms = millis();
    display_status("RECORDING", COL_RECORDING);
    display_clear_conversation();
    draw_touch_bar(true);
}

void setup() {
    M5.begin();
    Serial.begin(115200);
    display_init();
    draw_touch_bar(false);

    audio_capture_init();
    audio_playback_init();

    if (!secrets_load()) {
        display_status("ERROR", 0x8000);
        display_transcript("No secrets.json — flash LittleFS");
        while (true) delay(1000);
    }

    wifi_connect();
    enter_idle();
}

void loop() {
    M5.update();

    bool touching_bottom = false;
    if (M5.Touch.getCount() > 0) {
        auto t = M5.Touch.getDetail(0);
        if (t.y >= DISP_TOUCH_Y) touching_bottom = true;
    }

    switch (state) {
        case IDLE:
            if (touching_bottom)
                enter_recording();
            break;

        case RECORDING:
            audio_capture_tick();
            if (!touching_bottom || (millis() - state_ms >= REC_MAX_MS)) {
                audio_capture_stop();
                float secs = (float)audio_capture_length() / (SAMPLE_RATE * sizeof(int16_t));
                Serial.printf("Recorded %.1fs\n", secs);
                display_status("THINKING", COL_STT);
                draw_touch_bar(false);
                state = STT_PENDING;
            }
            break;

        case STT_PENDING:
            if (stt_transcribe(audio_capture_buffer(), audio_capture_length(),
                               transcript, sizeof(transcript))) {
                Serial.printf("Transcript: \"%s\"\n", transcript);
                display_transcript(transcript);
                display_status("THINKING", COL_LLM);
                state = LLM_PENDING;
            } else {
                display_transcript("Could not hear — try again");
                enter_idle();
            }
            break;

        case LLM_PENDING:
            if (llm_complete(transcript, llm_response, sizeof(llm_response))) {
                Serial.printf("LLM: \"%s\"\n", llm_response);
                display_response(llm_response);
                display_status("SPEAKING", COL_SPEAKING);
                if (!tts_speak(llm_response)) {
                    display_response("(TTS failed)");
                }
                enter_idle();
            } else {
                display_response("(No response)");
                enter_idle();
            }
            break;

        case SPEAKING:
            enter_idle();
            break;
    }
}
