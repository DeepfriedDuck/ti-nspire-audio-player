#!/usr/bin/env python3
"""Convert a WAV file to UART PDM stream for the Ndless audio player.

Output is a binary file of UART data bytes. Each byte's individual bits
form a 1-bit PDM stream at the UART data bit rate. After RC filtering
on the Tx pin, the average voltage reconstructs the audio waveform.

The UART sends LSB first, so bit 0 of each byte is the first PDM sample.
"""

import wave
import struct
import sys

# UART sends 10 bits per frame: 1 start + 8 data + 1 stop
# At 460800 baud: 46080 frames/sec, 368640 data bits/sec
# This is the effective PDM sample rate
BAUD_RATE = 115200
DATA_BITS_PER_FRAME = 8
BITS_PER_FRAME = 10
PDM_RATE = BAUD_RATE * DATA_BITS_PER_FRAME // BITS_PER_FRAME  # 368640

def wav_to_uart(wav_path):
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

    # Resample to PDM rate (linear interpolation)
    ratio = rate / PDM_RATE
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

    print(f"Resampled to {len(resampled)} PDM bits at {PDM_RATE}Hz")

    # Second-order delta-sigma modulation for better noise shaping
    bits = []
    error1 = 0.0
    error2 = 0.0
    for s in resampled:
        v = s + 2 * error1 - error2
        if v > 0:
            bits.append(1)
            quant = 1.0
        else:
            bits.append(0)
            quant = -1.0
        error2 = error1
        error1 = v - quant

    # Pack into bytes, LSB first (UART sends LSB first)
    while len(bits) % 8 != 0:
        bits.append(0)

    uart_bytes = []
    for i in range(0, len(bits), 8):
        byte = 0
        for b in range(8):
            byte |= bits[i + b] << b
        uart_bytes.append(byte)

    return uart_bytes

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <input.wav>")
        sys.exit(1)

    data = wav_to_uart(sys.argv[1])
    duration = len(data) / (BAUD_RATE / BITS_PER_FRAME)

    with open("song.bin", "wb") as f:
        f.write(bytes(data))

    print(f"Wrote song.bin: {len(data)} bytes ({len(data)/1024/1024:.1f}MB), {duration:.1f}s")
    print(f"UART baud rate: {BAUD_RATE}")
    print(f"Effective PDM rate: {PDM_RATE}Hz")

if __name__ == "__main__":
    main()
