#!/usr/bin/env python3
"""Convert a WAV file to UART PDM stream for the Ndless audio player.

Output is a binary file of UART data bytes. Each byte's individual bits
form a 1-bit PDM stream at the UART data bit rate. After RC filtering
on the Tx pin, the average voltage reconstructs the audio waveform.

The UART sends LSB first, so bit 0 of each byte is the first PDM sample.
"""

import json
import os
import wave
import struct
import sys
import pathlib
from PIL import Image, ImageOps
from io import BytesIO
import subprocess
import tempfile
# UART sends 10 bits per frame: 1 start + 8 data + 1 stop
# At 460800 baud: 46080 frames/sec, 368640 data bits/sec
# This is the effective PDM sample rate
BAUD_RATE = 115200
DATA_BITS_PER_FRAME = 8
BITS_PER_FRAME = 10
PDM_RATE = BAUD_RATE * DATA_BITS_PER_FRAME // BITS_PER_FRAME  # 368640
BASE_FMT = "<4sHIQ"
HEADER_MAGIC_BYTES = b'ATNS'
FILE_VERSION = 1
BASE_SIZE = struct.calcsize(BASE_FMT)
IMAGE_SIZE = (200, 200)

def _wav_to_uart(wav_path: pathlib.Path | str) -> list[int]:
    wav_path = pathlib.Path(wav_path)
    with wave.open(str(wav_path), 'rb') as w:
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

def _convert_audio_path_to_wave(audio_path: pathlib.Path) -> pathlib.Path:
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as temp_file:
        output_file_path = pathlib.Path(temp_file.name)

    result = subprocess.run([
        "ffmpeg",
        "-version",
    ], capture_output=True, text=True)
    if result.returncode != 0:
        print("ffmpeg not found in path")
        sys.exit(1)

    result = subprocess.run([
        "ffmpeg",
        "-y",
        "-i",
        str(audio_path.resolve()),
        "-f",
        "wav",
        str(output_file_path),
    ], capture_output=True, text=True)
    if result.returncode != 0:
        print("Ffmpeg threw an error:\n" + result.stderr)
        sys.exit(1)

    return output_file_path

        

def _path_to_bmp(image_path: pathlib.Path, mode: str ="RGB") -> bytes:
    img = Image.open(image_path).convert(mode)
    fitted = ImageOps.contain(img, IMAGE_SIZE, method=Image.Resampling.LANCZOS)
    canvas = Image.new(mode, IMAGE_SIZE, (0, 0, 0))
    x = (IMAGE_SIZE[0] - fitted.width) // 2
    y = (IMAGE_SIZE[1] - fitted.height) // 2
    canvas.paste(fitted, (x, y))
    buf = BytesIO()
    canvas.save(buf, format="BMP")
    canvas.save("resources/default_track.bmp", format="BMP")
    return buf.getvalue()


def _add_metadata(
    title: str,
    artist: str,
    uart_bytes: list[int],
    cover_image: pathlib.Path = pathlib.Path("resources/default_track.bmp"),
) -> bytes:
    print("Adding Metadata")
    cover_image_bytes = _path_to_bmp(cover_image)
    header_json = {"title": title, "artist": artist}
    header_bytes = json.dumps(header_json, separators=(",", ":")).encode("utf-8")
    header_len = BASE_SIZE + len(header_bytes)
    base_header = struct.pack(BASE_FMT, HEADER_MAGIC_BYTES, FILE_VERSION, header_len, len(cover_image_bytes))

    return base_header + header_bytes + cover_image_bytes + bytes(uart_bytes)

def create_song(name: str, artist: str, input_file: pathlib.Path | str, cover_image_path: pathlib.Path | str, output_folder_path: pathlib.Path | str = pathlib.Path("."), output_file_name: pathlib.Path | str = "song.tns"):
    input_file, cover_image_path, output_folder_path, output_file_name = (
        pathlib.Path(input_file),
        pathlib.Path(cover_image_path),
        pathlib.Path(output_folder_path),
        pathlib.Path(output_file_name),
    )

    os.makedirs(output_folder_path, exist_ok=True)
    wav_path = _convert_audio_path_to_wave(input_file)
    uart_bytes = _wav_to_uart(wav_path)
    file_bytes = _add_metadata(name, artist, uart_bytes, cover_image_path)

    with open(output_folder_path / output_file_name, "wb") as file:
        file.write(file_bytes)
    
def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <input.wav>")
        sys.exit(1)

    data = _wav_to_uart(sys.argv[1])
    duration = len(data) / (BAUD_RATE / BITS_PER_FRAME)

    with open("song.bin", "wb") as f:
        f.write(bytes(data))

    print(f"Wrote song.bin: {len(data)} bytes ({len(data)/1024/1024:.1f}MB), {duration:.1f}s")
    print(f"UART baud rate: {BAUD_RATE}")
    print(f"Effective PDM rate: {PDM_RATE}Hz")

if __name__ == "__main__":
    create_song(
        "Sweet Talkin Womman",
        "ELO",
        "./assets/samples/song.mp3",
        "./assets/samples/cover.png",
        "./resources",
        "Sweet_Talkin_Womman.tns",
    )