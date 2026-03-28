# TI-Nspire Audio Player

Ndless-based TI-Nspire audio player with:
- C runtime/player in `src/`
- ATNS container parsing and playback
- Python tooling (`tools/wav2uart.py`) for audio/container generation

## Project layout

- `src/` — Ndless C sources
- `tools/` — converter and helper scripts
- `assets/` — sample input media
- `resources/` — generated songs and assets
- `tests/` — Python tests and desktop test utilities
- `Makefile` — canonical Ndless build entrypoint

## Python tooling (PDM)

This repository uses PDM for Python dependencies.

### Install dependencies

```bash
pdm install
```

### Run converter

```bash
pdm run convert
```

The default sample conversion in `tools/wav2uart.py` uses:
- `assets/samples/song.mp3`
- `assets/samples/cover.png`

### Run tests

```bash
pdm install -G test
pdm run test
```

## Ndless build

```bash
make
```

## Deploy to calculator

```bash
make run
```
