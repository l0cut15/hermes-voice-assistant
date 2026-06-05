# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

Standalone WiFi AI agent voice controller running on the **M5Stack Core S3 SE** (ESP32-S3). Push-to-talk → local whisper.cpp STT → Hermes agent LLM → local Kokoro TTS → speaker. Built with PlatformIO + Arduino framework.

See `AGENT_VOICE_CONTROL_ARCHITECTURE.md` for the full design spec.

## Dev Environment

**VS Code + PlatformIO IDE extension** is the required setup. The extension installs the `pio` CLI and provides IntelliSense for Arduino/ESP32 headers. Without it, the LSP reports false errors for `M5Unified.h`, `Serial`, `M5`, etc. — those are not real.

**Python version note:** The system Python is 3.14, which is too new for the espressif32 platform (requires 3.10–3.13). The project uses a local mise environment with Python 3.13 (`mise.toml` in project root). Always invoke PlatformIO via `python -m platformio`, not `pio` directly — the `pio` shebang hardcodes `/usr/bin/python` (3.14).

```bash
python -m platformio run                      # compile
python -m platformio run -v                   # compile verbose
python -m platformio run --target upload      # compile + flash firmware over USB
python -m platformio run --target uploadfs    # flash LittleFS (data/ directory)
python -m platformio device monitor           # serial monitor at 115200 baud
```

Flash order on first setup: `upload` then `uploadfs`. Secrets survive in LittleFS — OTA firmware updates only need `upload`.

## Current Status

**All phases written and confirmed working on hardware.**

### What exists

```
platformio.ini              — board config, lib_deps
mise.toml                   — local Python 3.13 for PlatformIO (do not remove)
.gitignore                  — excludes data/secrets.json, .pio/
data/secrets.example.json   — template; copy to secrets.json and fill in
src/config.h                — SAMPLE_RATE, REC_MAX_BYTES constants
src/audio_capture.h/.cpp    — I2S PDM mic via M5Unified, PSRAM ring buffer, chunk-based recording
src/audio_playback.h/.cpp   — M5Unified Speaker, raw PCM playback
src/main.cpp                — Full state machine: IDLE → RECORDING → STT_PENDING → LLM_PENDING → SPEAKING
src/stt.cpp/.h              — multipart HTTP POST to whisper.cpp, WAV header, JSON parse
src/llm.cpp/.h              — HTTP POST to Hermes agent API, Bearer auth, response parse
src/tts.cpp/.h              — HTTP POST to Kokoro TTS, raw PCM buffered into PSRAM, speaker playback
src/display.cpp/.h          — M5GFX 4-zone layout: state bar / AI panel / transcript / touch bar
src/secrets.cpp/.h          — LittleFS mount, JSON parse into Secrets struct
```

## Secrets

`data/secrets.json` — loaded from LittleFS at boot, never compiled in, gitignored.

```json
{
  "wifi_ssid": "",
  "wifi_pass": "",
  "openai_key": "",
  "stt_host": "10.10.11.111",
  "stt_port": "7124",
  "hermes_host": "10.10.22.19",
  "hermes_port": "7237",
  "hermes_key": "your-api-server-key",
  "tts_host": "10.10.11.11",
  "tts_port": "7235"
}
```

`hermes_key` must match the `API_SERVER_KEY` set on the Hermes server. STT and TTS are local plain HTTP — no key needed.

Copy `data/secrets.example.json` → `data/secrets.json` and fill in before flashing.

## Architecture

### Pipeline

Touch held → record PCM into PSRAM → POST WAV to whisper.cpp → transcript → POST to Hermes agent API (Bearer auth) → response text → POST to Kokoro TTS → raw PCM buffered in PSRAM → speaker

### State Machine

```
IDLE → RECORDING → STT_PENDING → LLM_PENDING → SPEAKING → IDLE
                              (any state) → error text → IDLE
```

### Key Constraints

- **PSRAM for audio buffers** — always `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` for the record buffer (~256 KB) and TTS PCM buffer (~600 KB). Internal SRAM is only 512 KB total. Check for null after allocation.
- **M5Unified handles audio hardware** — use `M5.Mic` for recording and `M5.Speaker` for PCM playback. No manual I2S register writes needed. `M5.Speaker.setVolume(200)` is a good default.
- **Chunk-based recording** — `audio_capture_tick()` records 256 samples (~16 ms) per call. Call once per loop iteration while touch is held.
- **All network calls are plain HTTP** — use `HTTPClient`, not `WiFiClientSecure`. STT, LLM, and TTS are all local.
  - STT: `http://10.10.11.111:7124/inference` (whisper.cpp, multipart/form-data)
  - LLM: `http://10.10.22.19:7237/v1/chat/completions` (Hermes agent, OpenAI-compat, Bearer auth)
  - TTS: `http://10.10.11.11:7235/v1/audio/speech` (Kokoro, raw PCM response)
- **Hermes auth** — every LLM request must include `Authorization: Bearer <hermes_key>`.
- **WAV header** — prepend a 44-byte WAV header to raw PCM before POSTing to whisper.cpp.
- **HTTP/1.0 for TTS** — `http.useHTTP10(true)` prevents chunked transfer encoding from corrupting the PCM stream.
- **max_tokens 150** — keeps responses short for speaker playback.

### PlatformIO Environment

```ini
[env:m5stack-cores3]
platform = espressif32
board = m5stack-cores3
framework = arduino
board_build.filesystem = littlefs
board_build.partitions = default_16MB.csv
board_build.f_cpu = 240000000

lib_deps =
    m5stack/M5Unified @ ^0.2.4
    m5stack/M5GFX @ ^0.2.4
    bblanchon/ArduinoJson @ ^7.0.0
```

Note: `board_build.f_cpu` must be a plain integer — the `L` suffix is invalid in INI files and causes a config parse error.

## Build Phases

| Phase | Status | Description |
|-------|--------|-------------|
| 1 | **Done** | Audio loopback — touch → mic → PSRAM → speaker |
| 2 | **Done** | WiFi + whisper.cpp STT |
| 3 | **Done** | Full pipeline — STT → Hermes agent → Kokoro TTS |
| 4 | **Done** | Display + full state machine |
| 5 | Deferred | Wake word (ESP-SR WakeNet), volume control, battery indicator |

## Server Setup

### whisper.cpp (STT — port 7124)

```bash
cd servers/whisper-stt && docker compose up -d
```

Or bare process: `whisper-server --model models/ggml-small.en.bin --host 0.0.0.0 --port 7124`

### Hermes Agent API (LLM — port 7237)

Hermes must be installed and running as a systemd user service (`hermes-gateway`). To enable the API server, add to `~/.hermes/.env` on the host machine:

```bash
API_SERVER_ENABLED=true
API_SERVER_KEY=your-api-server-key
API_SERVER_PORT=7237
API_SERVER_HOST=0.0.0.0
```

Then restart the gateway:

```bash
systemctl --user restart hermes-gateway
```

Verify it's listening: `ss -tlnp | grep 7237`

The API server uses whatever model is configured as default in `~/.hermes/config.yaml`. The `model` field in requests is cosmetic — actual LLM is server-side config. Current default: `deepseek/deepseek-v4-flash` via Nous provider.

### Kokoro TTS (port 7235)

```bash
cd servers/kokoro-tts && docker compose up -d
```

Voice: `af_sky`. Output: raw PCM, 24 kHz, 16-bit signed mono.
