import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import wav2uart


def test_constants_are_consistent() -> None:
    assert wav2uart.HEADER_MAGIC_BYTES == b"ATNS"
    assert wav2uart.BASE_SIZE == struct.calcsize(wav2uart.BASE_FMT)


def test_add_metadata_layout(monkeypatch) -> None:
    fake_cover = b"BMFAKE"

    def fake_path_to_bmp(_path: Path) -> bytes:
        return fake_cover

    monkeypatch.setattr(wav2uart, "_path_to_bmp", fake_path_to_bmp)

    payload = wav2uart._add_metadata("Track", "Artist", [1, 2, 3], Path("cover.png"))

    magic, version, header_len, cover_size = struct.unpack(
        wav2uart.BASE_FMT,
        payload[: wav2uart.BASE_SIZE],
    )

    assert magic == wav2uart.HEADER_MAGIC_BYTES
    assert version == wav2uart.FILE_VERSION
    assert cover_size == len(fake_cover)

    meta_raw = payload[wav2uart.BASE_SIZE:header_len]
    meta = json.loads(meta_raw.decode("utf-8"))
    assert meta["title"] == "Track"
    assert meta["artist"] == "Artist"

    cover_start = header_len
    cover_end = cover_start + len(fake_cover)
    assert payload[cover_start:cover_end] == fake_cover
    assert payload[cover_end:] == bytes([1, 2, 3])
