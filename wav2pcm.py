#!/usr/bin/env python3
"""Convert a WAV file to 8-bit unsigned PCM for the Ndless PWM player."""

import wave
import struct
import sys

SAMPLE_RATE = 8000  # Must match SAMPLE_RATE in main.c

def wav_to_pcm(wav_path):
    with wave.open(wav_path, 'r') as w:
        nch = w.getnchannels()
        sampw = w.getsampwidth()
        rate = w.getframerate()
        nframes = w.getnframes()
        raw = w.readframes(nframes)

    # Decode to float samples [-1.0, 1.0], mono
    if sampw == 1:
        samples = [(b - 128) / 128.0 for b in raw[::nch]]
    elif sampw == 2:
        vals = struct.unpack(f'<{nframes * nch}h', raw)
        samples = [vals[i * nch] / 32768.0 for i in range(nframes)]
    else:
        sys.exit(f"Unsupported sample width: {sampw}")

    # Resample to target rate (linear interpolation)
    ratio = rate / SAMPLE_RATE
    total = int(len(samples) / ratio)
    resampled = []
    for i in range(total):
        pos = i * ratio
        idx = int(pos)
        frac = pos - idx
        if idx + 1 < len(samples):
            resampled.append(samples[idx] * (1 - frac) + samples[idx + 1] * frac)
        else:
            resampled.append(samples[idx])

    # Convert to unsigned 8-bit (0-255, 128 = silence)
    pcm = []
    for s in resampled:
        val = int((s + 1.0) * 127.5)
        if val < 0: val = 0
        if val > 255: val = 255
        pcm.append(val)

    return pcm

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <input.wav>")
        sys.exit(1)

    pcm = wav_to_pcm(sys.argv[1])
    duration = len(pcm) / SAMPLE_RATE

    with open("song.bin", "wb") as f:
        f.write(bytes(pcm))

    print(f"Wrote song.bin: {len(pcm)} bytes, {duration:.2f}s at {SAMPLE_RATE}Hz")

if __name__ == "__main__":
    main()
