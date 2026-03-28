#!/usr/bin/env python3
"""Convert a WAV file to a PDM C array for the Ndless player."""

import wave
import struct
import sys

SAMPLE_RATE = 50000  # Must match SAMPLE_RATE in main.c

def wav_to_pdm(wav_path):
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

    # Delta-sigma modulation: convert to 1-bit PDM
    bits = []
    error = 0.0
    for s in resampled:
        if s + error > 0:
            bits.append(1)
            error += s - 1.0
        else:
            bits.append(0)
            error += s + 1.0

    # Pack bits into bytes (MSB first)
    while len(bits) % 8 != 0:
        bits.append(0)

    pdm_bytes = []
    for i in range(0, len(bits), 8):
        byte = 0
        for b in range(8):
            byte |= bits[i + b] << (7 - b)
        pdm_bytes.append(byte)

    return pdm_bytes

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <input.wav>")
        sys.exit(1)

    pdm = wav_to_pdm(sys.argv[1])
    duration = len(pdm) * 8 / SAMPLE_RATE

    with open("song.bin", "wb") as f:
        f.write(bytes(pdm))

    print(f"Wrote song.bin: {len(pdm)} bytes, {duration:.2f}s at {SAMPLE_RATE}Hz")

if __name__ == "__main__":
    main()
