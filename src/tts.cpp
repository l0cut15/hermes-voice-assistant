#include "tts.h"
#include "secrets.h"
#include <M5Unified.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

static const size_t   TTS_BUF         = 600 * 1024;  // 600 KB ~= 12 s at 24 kHz 16-bit
static const uint32_t TTS_SAMPLE_RATE = 24000;

bool tts_speak(const char* text) {
    if (!g_secrets.tts_host[0]) {
        Serial.println("TTS: tts_host not set in secrets.json");
        return false;
    }

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%s/v1/audio/speech",
             g_secrets.tts_host, g_secrets.tts_port);

    JsonDocument req;
    req["model"]           = "kokoro";
    req["voice"]           = g_secrets.tts_voice[0] ? g_secrets.tts_voice : "af_heart";
    req["input"]           = text;
    req["response_format"] = "pcm";  // raw 16-bit signed PCM @ 24 kHz
    String body;
    serializeJson(req, body);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(30000);
    http.useHTTP10(true);  // disable chunked transfer encoding — getStreamPtr reads raw bytes
    http.addHeader("Content-Type", "application/json");

    int code = http.POST(body);
    if (code != 200) {
        Serial.printf("TTS: HTTP %d\n", code);
        http.end();
        return false;
    }

    uint8_t* buf = (uint8_t*)heap_caps_malloc(TTS_BUF, MALLOC_CAP_SPIRAM);
    if (!buf) {
        Serial.println("TTS: PSRAM alloc failed");
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t total = 0;
    uint32_t deadline = millis() + 20000;
    while (millis() < deadline && total < TTS_BUF) {
        int avail = stream->available();
        if (avail > 0) {
            size_t to_read = min((size_t)avail, TTS_BUF - total);
            total += stream->readBytes(buf + total, to_read);
        } else if (!http.connected()) {
            // Connection closed — drain any bytes still in the TCP buffer
            while (total < TTS_BUF) {
                int rem = stream->available();
                if (rem <= 0) break;
                total += stream->readBytes(buf + total, min((size_t)rem, TTS_BUF - total));
            }
            break;
        } else {
            delay(5);
        }
    }
    http.end();

    if (total < 2) {
        Serial.println("TTS: no audio data received");
        heap_caps_free(buf);
        return false;
    }

    Serial.printf("TTS: %u bytes (%.1f s) — playing\n",
                  total, (float)total / (TTS_SAMPLE_RATE * sizeof(int16_t)));

    M5.Mic.end();
    auto spk_cfg = M5.Speaker.config();
    spk_cfg.sample_rate = TTS_SAMPLE_RATE;  // 16kHz — no resampling needed
    spk_cfg.dma_buf_len = 1024;
    M5.Speaker.config(spk_cfg);
    M5.Speaker.begin();
    M5.Speaker.setVolume(200);
    M5.Speaker.playRaw((const int16_t*)buf, total / sizeof(int16_t),
                       TTS_SAMPLE_RATE, false, 1);

    while (M5.Speaker.isPlaying()) {
        M5.update();
        delay(20);
    }

    heap_caps_free(buf);
    return true;
}
