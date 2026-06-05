#include "stt.h"
#include "secrets.h"
#include "config.h"
#include <M5Unified.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char* BOUNDARY = "----AgentVoiceBoundary";

static void write_wav_header(uint8_t* dst, uint32_t pcm_bytes) {
    uint32_t file_size   = 36 + pcm_bytes;
    uint16_t channels    = 1;
    uint16_t bits        = 16;
    uint32_t sample_rate = SAMPLE_RATE;
    uint32_t byte_rate   = sample_rate * channels * bits / 8;
    uint16_t block_align = channels * bits / 8;
    uint32_t fmt_size    = 16;
    uint16_t audio_fmt   = 1;  // PCM

    memcpy(dst +  0, "RIFF",      4);
    memcpy(dst +  4, &file_size,  4);
    memcpy(dst +  8, "WAVE",      4);
    memcpy(dst + 12, "fmt ",      4);
    memcpy(dst + 16, &fmt_size,   4);
    memcpy(dst + 20, &audio_fmt,  2);
    memcpy(dst + 22, &channels,   2);
    memcpy(dst + 24, &sample_rate,4);
    memcpy(dst + 28, &byte_rate,  4);
    memcpy(dst + 32, &block_align,2);
    memcpy(dst + 34, &bits,       2);
    memcpy(dst + 36, "data",      4);
    memcpy(dst + 40, &pcm_bytes,  4);
}

bool stt_transcribe(const uint8_t* pcm_buf, size_t pcm_len,
                    char* transcript, size_t transcript_size) {
    // Build multipart body: preamble + WAV header + PCM + postamble
    char preamble[256];
    int pre_len = snprintf(preamble, sizeof(preamble),
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
        "Content-Type: audio/wav\r\n"
        "\r\n",
        BOUNDARY);

    char postamble[256];
    int post_len = snprintf(postamble, sizeof(postamble),
        "\r\n--%s--\r\n",
        BOUNDARY);

    size_t body_size = (size_t)pre_len + 44 + pcm_len + (size_t)post_len;
    uint8_t* body = (uint8_t*)heap_caps_malloc(body_size, MALLOC_CAP_SPIRAM);
    if (!body) {
        Serial.println("STT: PSRAM alloc failed");
        return false;
    }

    uint8_t* p = body;
    memcpy(p, preamble, pre_len);   p += pre_len;
    write_wav_header(p, pcm_len);   p += 44;
    memcpy(p, pcm_buf, pcm_len);    p += pcm_len;
    memcpy(p, postamble, post_len);

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%s/inference",
             g_secrets.stt_host, g_secrets.stt_port);
    Serial.printf("STT: POST %u bytes to %s\n", body_size, url);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(30000);

    char ct[80];
    snprintf(ct, sizeof(ct), "multipart/form-data; boundary=%s", BOUNDARY);
    http.addHeader("Content-Type", ct);

    int code = http.POST(body, body_size);
    heap_caps_free(body);

    if (code != 200) {
        Serial.printf("STT: HTTP %d — %s\n", code, http.getString().c_str());
        http.end();
        return false;
    }

    String resp = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) {
        Serial.printf("STT: JSON parse failed, raw: %s\n", resp.c_str());
        return false;
    }

    const char* text = doc["text"] | "";
    if (!text[0]) {
        Serial.println("STT: empty transcript");
        return false;
    }

    strlcpy(transcript, text, transcript_size);
    return true;
}
