#pragma once

// Download PCM audio from OpenAI TTS into PSRAM and start playback.
// Returns false on error. Caller must wait for M5.Speaker.isPlaying() == false.
bool tts_speak(const char* text);
