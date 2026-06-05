#pragma once
#include <stdint.h>
#include <stddef.h>

void audio_playback_init();
void audio_playback_start(const uint8_t* buf, size_t len);  // raw 16-bit PCM
bool audio_playback_is_playing();
