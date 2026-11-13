# Captured blinking-screen replay preparation

`mono-blink.raw.gz` contains one complete visible/hidden blink cycle from
`sony-td-mono-blink.bin`. Decompress to obtain one byte per 125 ns sample.
Bits 0-5 retain the original analyser channel order, **not GPIO order**:

| Bit | Signal | Current GPIO |
|-----|--------|--------------|
| 0 | FLSCLK | 48 |
| 1 | FLSDATA | 47 |
| 2 | FLCLEAR | 41 |
| 3 | FLSTB | 42 |
| 4 | FLGDATA | 39 |
| 5 | FLGCLK | 40 |

The payload preserves all six sampled levels, including simultaneous
transitions, actual clock gaps, CLEAR pulses and STB timing. No waveform
resynthesis, zero-flush interval or scheduler gap is inserted. It includes
the captured frequency, PROGRAM/MONO and red-display words, not just text.
The JSON records source/payload hashes and the exact extraction boundaries.
Timing accuracy remains limited to the original 8 MHz acquisition.

Regenerate with:

```sh
python3 firmware/scripts/prepare_screen_replay.py \
  /path/to/sony-td-mono-blink.bin firmware/tests/fixtures/screen
```

`mono-blink.rle` is embedded in the screen firmware. Its 17-byte header is
`SCR2`, little-endian uint32 sample count, uint32 frame count and a five-byte
little-endian initial segment word. Grid preload is OUT1 HIGH. Each following
three-byte record is a little-endian uint16 run length and a six-bit state.
The firmware expands it into PSRAM before sending one continuous 8 MHz LCD
parallel DMA transaction, not separate per-frame transfers.

The bus uses GPIO11/12 for unused data bits, GPIO13 for WR and GPIO14 for D/C;
all four must remain disconnected from Sony. Firmware wraps the unchanged
fixture with a 324-microsecond initialization prefix and at least a
20-microsecond blank-stop suffix. Prefix: CL LOW, STB HIGH, 40 shifts into
both registers, with initial segment word and grid OUT1 HIGH. Suffix/idle:
`0x21`, clocks HIGH, all other lines LOW, retaining actual grid CL LOW.
Stop requests during replay wait for completion and retain this override.
Explicit Release all pins afterwards removes it.

Buffered analyser captures of firmware 0.3.5 verify the deliberate
initialization shifts, grid preload, scan data/markers and no unwanted
shutdown clocks. Analogue margins and actual correct live display behavior
remain unverified. Do not use the older approximate text API as a substitute.

## Separate Frequency acquisition

`frequency.raw.gz`, `frequency.rle` and `frequency.json` preserve 128
consecutive complete frames from `sony-td-satellite-lnc-off.bin`. Extraction
checks all nine Frequency glyph words in slots 14-22 for every frame, with
40 segment clocks and one grid clock per slot. All other words and all
six recorded signals are preserved, not replaced with zeros.

```sh
python3 firmware/scripts/prepare_screen_replay.py --frequency \
  /path/to/sony-td-satellite-lnc-off.bin firmware/tests/fixtures/screen
```

Captured duration is 1.001940125 seconds. The same initialization/blank-stop
wrapper, DMA alignment and unverified live-display limitations apply.
