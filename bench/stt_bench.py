#!/usr/bin/env python3
"""
STT server latency benchmark.

Sends a WAV file to each whisper.cpp server N times and compares latency
and transcription quality side-by-side.

Usage:
    # Use a pre-recorded WAV (recommended — real mic audio)
    python3 bench/stt_bench.py --file bench/sample.wav
    python3 bench/stt_bench.py --file bench/sample.wav --runs 10 -v

    # Generate synthetic audio via macOS say (fallback, no mic needed)
    python3 bench/stt_bench.py --phrase "What is the weather like today?"
    python3 bench/stt_bench.py   # runs all default phrases via say

Requirements:
    pip install requests
"""

import argparse
import os
import statistics
import subprocess
import sys
import tempfile
import time

try:
    import requests
except ImportError:
    sys.exit("Missing dependency: pip install requests")

# ── Server definitions ────────────────────────────────────────────────────────

SERVERS = {
    "mac  /inference         (10.10.11.111:7124)": {
        "url":   "http://10.10.11.111:7124/inference",
        "field": "file",
        "extra": {},  # native whisper.cpp — no extra fields needed
    },
    "linux /v1/audio/transcriptions (10.10.11.11:7124)": {
        "url":   "http://10.10.11.11:7124/v1/audio/transcriptions",
        "field": "file",
        "extra": {"model": "whisper-1"},  # OpenAI-compat requires model field
    },
}

# ── Test phrases ──────────────────────────────────────────────────────────────

DEFAULT_PHRASES = [
    "What is the weather like today?",
    "Set a timer for ten minutes.",
    "Turn off the kitchen lights.",
    "The quick brown fox jumps over the lazy dog.",
    "Play some jazz music in the living room.",
]

# ── Audio generation ──────────────────────────────────────────────────────────

def make_wav(text: str, out_path: str) -> int:
    """
    Generate a 16 kHz 16-bit mono WAV via macOS `say` + `afconvert`.
    Returns file size in bytes.
    """
    aiff = out_path + ".aiff"
    try:
        subprocess.run(
            ["say", "-o", aiff, text],
            check=True, capture_output=True,
        )
        subprocess.run(
            ["afconvert", "-f", "WAVE", "-d", "LEI16@16000", "-c", "1", aiff, out_path],
            check=True, capture_output=True,
        )
    finally:
        if os.path.exists(aiff):
            os.unlink(aiff)
    return os.path.getsize(out_path)


# ── Transcription request ─────────────────────────────────────────────────────

def transcribe(url: str, wav_path: str, extra: dict, timeout: int = 60) -> tuple:
    """
    POST WAV to url as multipart/form-data.
    Returns (latency_seconds: float, transcript: str).
    Raises on HTTP error or non-200 status.
    """
    with open(wav_path, "rb") as f:
        wav_bytes = f.read()

    t0 = time.monotonic()
    resp = requests.post(
        url,
        files={"file": ("audio.wav", wav_bytes, "audio/wav")},
        data=extra,
        timeout=timeout,
    )
    latency = time.monotonic() - t0

    if resp.status_code != 200:
        raise RuntimeError(f"HTTP {resp.status_code}: {resp.text[:200]}")

    j = resp.json()
    text = j.get("text", "").strip()
    return latency, text


# ── Benchmark runner ──────────────────────────────────────────────────────────

def run_bench_file(wav_path: str, runs: int, verbose: bool) -> None:
    """Benchmark using a single pre-recorded WAV file."""
    sep = "─" * 70
    size = os.path.getsize(wav_path)
    # Best-effort duration: assume 16kHz 16-bit mono (44-byte WAV header)
    duration = max(0, (size - 44)) / (16000 * 2)

    print(f"\n{sep}")
    print(f"File    : {wav_path}")
    print(f"Audio   : {size:,} bytes  (~{duration:.1f}s)\n")

    _run_servers(wav_path, runs, verbose)
    print(sep)


def run_bench_phrases(phrases: list, runs: int, verbose: bool) -> None:
    """Benchmark using macOS say-generated audio for each phrase."""
    sep = "─" * 70

    for phrase in phrases:
        print(f"\n{sep}")
        print(f'Phrase  : "{phrase}"')

        with tempfile.TemporaryDirectory() as tmpdir:
            wav = os.path.join(tmpdir, "test.wav")
            size = make_wav(phrase, wav)
            duration = (size - 44) / (16000 * 2)
            print(f"Audio   : {size:,} bytes  ({duration:.2f}s @ 16 kHz 16-bit mono)\n")
            _run_servers(wav, runs, verbose)

    print(sep)


def _run_servers(wav: str, runs: int, verbose: bool) -> None:
    results = {}
    for name, cfg in SERVERS.items():
        latencies = []
        transcripts = []
        errors = 0

        for i in range(runs):
            try:
                lat, txt = transcribe(cfg["url"], wav, cfg["extra"])
                latencies.append(lat)
                transcripts.append(txt)
                if verbose:
                    print(f"  [{i+1}/{runs}] {name.split()[0]:5s}  {lat:.2f}s  → {txt!r}")
            except Exception as e:
                errors += 1
                if verbose:
                    print(f"  [{i+1}/{runs}] {name.split()[0]:5s}  ERROR: {e}")

        results[name] = (latencies, transcripts, errors)

    for name, (latencies, transcripts, errors) in results.items():
        print(f"  {name}")
        if not latencies:
            print(f"    ALL {runs} RUNS FAILED\n")
            continue

        mean = statistics.mean(latencies)
        med  = statistics.median(latencies)
        mn   = min(latencies)
        mx   = max(latencies)
        p95  = sorted(latencies)[int(len(latencies) * 0.95)]
        best = max(set(transcripts), key=transcripts.count)

        print(f"    transcript : {best!r}")
        print(f"    mean {mean:.2f}s   median {med:.2f}s   "
              f"min {mn:.2f}s   max {mx:.2f}s   p95 {p95:.2f}s"
              + (f"   errors {errors}" if errors else ""))
        print()


# ── Connectivity pre-check ────────────────────────────────────────────────────

def check_servers() -> bool:
    ok = True
    for name, cfg in SERVERS.items():
        host_port = cfg["url"].split("/")[2]
        try:
            r = requests.get(f"http://{host_port}/", timeout=3)
            # Any response (even 404) means the server is reachable
            print(f"  ✓ {name.strip()}")
        except requests.exceptions.ConnectionError:
            print(f"  ✗ {name.strip()}  — UNREACHABLE")
            ok = False
        except Exception:
            print(f"  ✓ {name.strip()}  (responded)")
    return ok


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="STT server latency benchmark")
    parser.add_argument("--runs",   type=int, default=5,
                        help="Requests per server (default 5)")
    parser.add_argument("--file",   type=str, default=None,
                        help="Pre-recorded WAV file to use (skips say generation)")
    parser.add_argument("--phrase", type=str, default=None,
                        help="Generate audio from this phrase via macOS say")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Print result of every individual request")
    args = parser.parse_args()

    print("STT Benchmark")
    print(f"  {args.runs} runs × {len(SERVERS)} servers\n")
    print("Checking connectivity …")
    check_servers()

    if args.file:
        if not os.path.exists(args.file):
            sys.exit(f"File not found: {args.file}")
        run_bench_file(args.file, args.runs, args.verbose)
    else:
        phrases = [args.phrase] if args.phrase else DEFAULT_PHRASES
        run_bench_phrases(phrases, args.runs, args.verbose)


if __name__ == "__main__":
    main()
