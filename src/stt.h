#pragma once
#include <stdint.h>
#include <stddef.h>

// POST pcm_buf as a WAV file to local whisper.cpp, fill transcript on success.
bool stt_transcribe(const uint8_t* pcm_buf, size_t pcm_len,
                    char* transcript, size_t transcript_size);
