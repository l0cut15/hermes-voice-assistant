#!/bin/bash
# Start the whisper.cpp STT server.
# Requires whisper-cpp installed (brew install whisper-cpp on macOS).
# On first run, download the model:
#   whisper-cpp --download-model small.en

MODEL="${1:-models/ggml-small.en.bin}"
PORT="${2:-7124}"

echo "Starting whisper-server on port $PORT with model $MODEL"
whisper-server \
  --model "$MODEL" \
  --host 0.0.0.0 \
  --port "$PORT" \
  --inference-path /inference
