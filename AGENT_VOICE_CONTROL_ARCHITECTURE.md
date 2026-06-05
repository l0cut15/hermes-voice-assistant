# AI Agent Voice Control — Architecture

M5Stack Core S3 SE standalone WiFi voice assistant. Touch to speak, fully local pipeline (except the Hermes agent which connects to cloud/local LLM via its configured provider).

---

## Pipeline

```
[Touch screen — bottom zone held]
        │
        ▼
[Record PCM from SPM1423 mic — I2S PDM mode]
[16 kHz, 16-bit mono, stored in PSRAM ring buffer]
        │
        ▼
[HTTP POST → whisper.cpp]              port 7124
[endpoint: /inference]
[body: multipart/form-data, file=WAV]
        │
        ▼
[Transcript text]
        │
        ▼
[HTTP POST → Hermes Agent API]         port 7237
[endpoint: /v1/chat/completions]
[Authorization: Bearer <hermes_key>]
[model: "hermes-agent"]
        │
        ▼
[Response text]
        │
        ▼
[HTTP POST → Kokoro TTS]               port 7235
[endpoint: /v1/audio/speech]
[response_format: pcm, 24 kHz 16-bit mono]
[http.useHTTP10(true) — avoids chunked encoding corruption]
        │
        ▼
[PCM buffered in PSRAM → AW88298 amp → Speaker]
```

---

## State Machine

```
IDLE
  │  touch held in bottom zone
  ▼
RECORDING  ── touch released / 8 s timeout ──▶  STT_PENDING
  │  mic active, PCM into PSRAM                       │ whisper.cpp call
  │                                                   ▼
  │                                            LLM_PENDING
  │                                                   │ Hermes agent call
  │                                                   ▼
  │                                            SPEAKING
  │                                                   │ Kokoro TTS + playRaw
  └───────────────────────────────────────────────── IDLE

Any state ── STT/LLM/TTS failure ──▶ show error text ──▶ IDLE
```

---

## Module Structure

```
src/
  main.cpp          — setup(), loop(), 5-state machine, WiFi, touch input
  audio_capture.cpp — SPM1423 PDM mic via M5Unified, PSRAM ring buffer, chunk recording
  audio_capture.h
  audio_playback.cpp — M5Unified Speaker, raw PCM playback
  audio_playback.h
  stt.cpp           — WAV header + multipart POST to whisper.cpp, JSON parse
  stt.h
  llm.cpp           — HTTP POST to Hermes agent API, Bearer auth, response parse
  llm.h
  tts.cpp           — HTTP POST to Kokoro, PCM stream into PSRAM, speaker playback
  tts.h
  display.cpp       — 4-zone layout: state bar / AI panel / transcript / touch bar
  display.h
  secrets.cpp       — LittleFS mount, JSON parse into Secrets struct
  secrets.h
  config.h          — SAMPLE_RATE=16000, REC_MAX_BYTES=256*1024
```

---

## Display Layout

Screen: 320 × 240 px

| Zone | Y range | Content |
|------|---------|---------|
| State bar | 0–50 | Pipeline state, coloured background |
| AI panel | 50–175 | LLM response, font size 2, word-wrapped |
| Transcript | 175–205 | STT output, font size 1 |
| Touch zone | 205–240 | "Hold to speak" / "Recording…" bar |

State bar colours (RGB565):

| State | Colour |
|-------|--------|
| READY | `0x0010` navy |
| RECORDING | `0xA000` dark red |
| THINKING (STT) | `0x8400` dark orange |
| THINKING (LLM) | `0x4010` dark purple |
| SPEAKING | `0x0400` dark green |

---

## Key Implementation Details

### I2S Peripheral Sharing
The SPM1423 mic and AW88298 speaker share the I2S peripheral on the Core S3 SE. Hard rule: always call `M5.Speaker.end()` before `M5.Mic.begin()` and `M5.Mic.end()` before `M5.Speaker.begin()`. Failure to do so causes "register I2S object to platform failed" and silence.

### Speaker Stack Size
`dma_buf_len` in `speaker_config_t` controls the speaker task stack: `stack = 1280 + dma_buf_len * 4`. Default `dma_buf_len=256` gives a 2304-byte stack which overflows. Set `dma_buf_len=1024` → 5376-byte stack.

### HTTP/1.0 for TTS
Kokoro returns `Transfer-Encoding: chunked`. `HTTPClient::getStreamPtr()` returns the raw TCP stream without decoding chunk headers, so they land in the PCM buffer as noise. `http.useHTTP10(true)` forces HTTP/1.0 and eliminates chunked encoding.

### Hermes Agent Auth
Every request to the Hermes API requires a Bearer token matching `API_SERVER_KEY` on the server:
```cpp
http.addHeader("Authorization", auth_header);  // "Bearer <hermes_key>"
```
The `model` field in requests is cosmetic — the actual LLM is determined by the server's `~/.hermes/config.yaml`.

### PSRAM Allocation
All large buffers must use PSRAM — internal SRAM is only 512 KB:
```cpp
heap_caps_malloc(size, MALLOC_CAP_SPIRAM)
```
Always check for null. Record buffer: 256 KB. TTS PCM buffer: 600 KB.

### WAV Header for STT
whisper.cpp `/inference` expects a WAV file. Prepend a 44-byte WAV header to raw PCM before POSTing:
```
RIFF chunk → fmt subchunk (PCM, 1 ch, 16000 Hz, 16-bit) → data subchunk
```

---

## Server Configuration

### whisper.cpp (port 7124)

Endpoint used: `POST /inference` (multipart/form-data, `file` field only).

**Docker (recommended):**
```bash
mkdir -p servers/whisper-stt/models
wget -O servers/whisper-stt/models/ggml-small.en.bin \
  https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.en.bin

cd servers/whisper-stt && docker compose up -d
```

**macOS bare process:**
```bash
whisper-server --model models/ggml-small.en.bin --host 0.0.0.0 --port 7124
```

Benchmarked on 18 s of speech: Apple Silicon ~0.25 s, AMD 5900X Docker CPU ~3.5 s.

### Hermes Agent API (port 7237)

OpenAI-compatible API server built into Hermes. Runs as a systemd user service (`hermes-gateway`).

**Enable the API server** — add to `~/.hermes/.env` on the host machine:

```bash
API_SERVER_ENABLED=true
API_SERVER_KEY=your-api-server-key   # must match hermes_key in secrets.json
API_SERVER_PORT=7237
API_SERVER_HOST=0.0.0.0              # bind to all interfaces so the device can reach it
```

**Restart to apply:**
```bash
systemctl --user restart hermes-gateway
```

**Verify:**
```bash
ss -tlnp | grep 7237
curl http://localhost:7237/v1/health
# → {"status": "ok", "platform": "hermes-agent"}
```

**Smoke test:**
```bash
curl http://localhost:7237/v1/chat/completions \
  -H "Authorization: Bearer your-api-server-key" \
  -H "Content-Type: application/json" \
  -d '{"model": "hermes-agent", "messages": [{"role": "user", "content": "Say hello"}]}'
```

The active LLM is set in `~/.hermes/config.yaml` (`model.default`). Current: `deepseek/deepseek-v4-flash` via Nous provider. The `model` field in API requests is accepted but cosmetic — the server decides the actual model.

Auth is always required (`API_SERVER_KEY` enforced) when `API_SERVER_HOST=0.0.0.0`.

### Kokoro TTS (port 7235)

See `servers/kokoro-tts/docker-compose.yml`. CPU-only, runs on any x86-64 host.

Voice used: `af_sky`. Output: raw PCM, 24 kHz, 16-bit signed, mono, little-endian.

---

## Memory Budget

| Buffer | Location | Size |
|--------|----------|------|
| Audio record (8 s @ 16 kHz 16-bit) | PSRAM | 256 KB |
| TTS PCM (12 s @ 24 kHz 16-bit) | PSRAM | 600 KB |
| LLM response string | heap | ~1 KB |
| Transcript string | heap | ~512 B |
| **Total PSRAM** | | ~856 KB of 8 MB |

---

## Secrets File

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

`openai_key` is present in the struct but not currently used — TTS is fully local via Kokoro.

---

## Future Work

| Feature | Notes |
|---------|-------|
| Wake word | ESP-SR WakeNet, ~200 KB flash, hands-free trigger |
| Multi-turn memory | Keep `messages[]` capped at 6 turns |
| Volume control | Side buttons B/C |
| Battery indicator | M5.Power.getBatteryLevel() |
| Silence detection | Energy threshold on mic input to skip silent recordings |
