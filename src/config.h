#pragma once
#include <stdint.h>
#include <stddef.h>

static const uint32_t SAMPLE_RATE   = 16000;
static const size_t   REC_MAX_BYTES = 256 * 1024;  // 8 s @ 16 kHz 16-bit mono
