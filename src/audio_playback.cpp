#include "audio_playback.h"
#include "config.h"
#include <M5Unified.h>

void audio_playback_init() {
    // Bump dma_buf_len to max — spk_task stack = 1280 + dma_buf_len*4, so 1024 → ~5 KB.
    // Don't call begin() here; mic claims I2S first, speaker begins on demand.
    auto cfg = M5.Speaker.config();
    cfg.dma_buf_len = 1024;
    M5.Speaker.config(cfg);
    M5.Speaker.setVolume(200);
}

void audio_playback_start(const uint8_t* buf, size_t len) {
    M5.Mic.end();      // release I2S before speaker claims it
    M5.Speaker.begin();
    M5.Speaker.stop();
    M5.Speaker.playRaw((const int16_t*)buf, len / sizeof(int16_t), SAMPLE_RATE, false, 1);
}

bool audio_playback_is_playing() {
    return M5.Speaker.isPlaying();
}
