#include "audio_capture.h"
#include "config.h"
#include <M5Unified.h>
#include <esp_heap_caps.h>

static const size_t CHUNK_SAMPLES = 256;  // 16 ms per tick at 16 kHz

static uint8_t* _buf = nullptr;
static size_t   _len = 0;

void audio_capture_init() {
    _buf = (uint8_t*)heap_caps_malloc(REC_MAX_BYTES, MALLOC_CAP_SPIRAM);
    if (!_buf) {
        Serial.println("ERROR: Failed to allocate PSRAM record buffer");
        return;
    }

    auto cfg = M5.Mic.config();
    cfg.sample_rate = SAMPLE_RATE;
    cfg.stereo = false;
    M5.Mic.config(cfg);
}

void audio_capture_start() {
    _len = 0;
    M5.Speaker.end();  // release I2S before mic claims it
    M5.Mic.begin();
}

bool audio_capture_tick() {
    if (_len + CHUNK_SAMPLES * sizeof(int16_t) > REC_MAX_BYTES) return false;
    M5.Mic.record((int16_t*)(_buf + _len), CHUNK_SAMPLES, SAMPLE_RATE);
    _len += CHUNK_SAMPLES * sizeof(int16_t);
    return true;
}

void audio_capture_stop() {
    M5.Mic.end();
}

uint8_t* audio_capture_buffer() { return _buf; }
size_t   audio_capture_length()  { return _len; }
