#pragma once
#include <stdint.h>
#include <stddef.h>

void     audio_capture_init();
void     audio_capture_start();
bool     audio_capture_tick();    // call once per loop while recording; false = buffer full
void     audio_capture_stop();
uint8_t* audio_capture_buffer();
size_t   audio_capture_length();  // bytes recorded
