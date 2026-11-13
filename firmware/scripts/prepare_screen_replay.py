#!/usr/bin/env python3
"""Extract measured display replay fixtures; never connect to an ESP."""

import argparse
from bisect import bisect_left
import gzip
import hashlib
import json
from pathlib import Path
import re
import struct


def encode_runs(payload, frames, initial_segment=0):
    if not payload or any(state > 63 for state in payload):
        raise ValueError("Expected nonempty six-bit waveform")
    runs = bytearray(b"SCR2" + struct.pack("<II", len(payload), frames) +
                    initial_segment.to_bytes(5, "little"))
    previous = payload[0]
    count = 0
    for state in payload:
        if state != previous or count == 65535:
            runs.extend(struct.pack("<HB", count, previous))
            previous, count = state, 0
        count += 1
    runs.extend(struct.pack("<HB", count, previous))
    return runs


def prepare(source, destination, frequency=False):
    raw = source.read_bytes()
    # Two seconds cover a full blink cycle in this five-second acquisition.
    window = raw[:16_000_000]
    lines = [
        window.translate(bytes((value >> bit) & 1 for value in range(256)))
        for bit in range(6)
    ]

    def rises(bit):
        return [match.start() + 1 for match in re.finditer(b"\x00\x01", lines[bit])]

    latches, segment, grid = rises(3), rises(0), rises(5)
    records = []
    for previous, latch in zip(latches, latches[1:]):
        clocks = segment[bisect_left(segment, previous):bisect_left(segment, latch)]
        grids = grid[bisect_left(grid, previous):bisect_left(grid, latch)]
        word = 0
        for clock in clocks:
            word = (word << 1) | lines[1][clock]
        records.append((latch, len(clocks), len(grids), word,
                        len(grids) == 1 and lines[4][grids[0]] == 1))

    frames = []
    for index, record in enumerate(records):
        if not record[4]:
            continue
        frame = records[index:index + 26]
        if len(frame) == 26 and all(row[1:3] == (40, 1) for row in frame):
            frames.append((record[0], frame[24][3], frame[25][3]))

    if frequency:
        expected = (
            0x03A1E8C400, 0x078210F800, 0x046319B400,
            0x0783F8B800, 0x05B318C400, 0x043D18F800,
            0x0783F8B800, 0x0084236800, 0x004217843F,
        )
        selected = []
        for index, record in enumerate(records):
            if not record[4]:
                continue
            frame = records[index:index + 26]
            if (len(frame) == 26 and
                    all(row[1:3] == (40, 1) for row in frame) and
                    tuple(row[3] for row in frame[14:23]) == expected):
                selected.append((index, record[0]))
            elif selected:
                break
            if len(selected) == 129:
                break
        if (len(selected) != 129 or
                any(b[0] - a[0] != 26 for a, b in zip(selected, selected[1:]))):
            raise ValueError("No 128 consecutive complete Frequency frames")
        start, end = selected[0][1], selected[-1][1]
        payload = window[start:end].translate(bytes(value & 63 for value in range(256)))
        destination.mkdir(parents=True, exist_ok=True)
        (destination / "frequency.raw.gz").write_bytes(gzip.compress(payload, mtime=0))
        initial_segment = records[selected[0][0]][3]
        (destination / "frequency.rle").write_bytes(
            encode_runs(payload, 128, initial_segment))
        metadata = {
            "source": source.name,
            "source_sha256": hashlib.sha256(raw).hexdigest(),
            "sample_rate_hz": 8_000_000,
            "start_sample": start,
            "end_sample_exclusive": end,
            "samples": len(payload),
            "duration_seconds": len(payload) / 8_000_000,
            "frames": 128,
            "initial_segment_word": f"{initial_segment:010X}",
            "initial_grid_word": "0000000001",
            "payload_sha256": hashlib.sha256(payload).hexdigest(),
            "text": "Frequency",
            "status": "Captured replay; physical output unverified",
        }
        (destination / "frequency.json").write_text(json.dumps(metadata, indent=2) + "\n")
        print(json.dumps(metadata, indent=2))
        return

    transitions = []
    for frame in frames:
        if not transitions or frame[1:] != transitions[-1][1:]:
            transitions.append(frame)
    # Visible -> hidden -> visible, using original PROGRAM/MONO/red words.
    visible, hidden = (0x998, 0x800022), (0x180, 0x2)
    for first, second, third in zip(transitions, transitions[1:], transitions[2:]):
        if first[1:] == visible and second[1:] == hidden and third[1:] == visible:
            start, end = first[0], third[0]
            break
    else:
        raise ValueError("No complete expected blink cycle in the capture")

    payload = window[start:end].translate(bytes(value & 63 for value in range(256)))
    cycle_frames = [frame for frame in frames if start <= frame[0] < end]
    if len(cycle_frames) != 130:
        raise ValueError(f"Expected 130 frames, got {len(cycle_frames)}")
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "mono-blink.raw.gz").write_bytes(gzip.compress(payload, mtime=0))
    initial_segment = next(row[3] for row in records if row[0] == start)
    (destination / "mono-blink.rle").write_bytes(
        encode_runs(payload, len(cycle_frames), initial_segment))
    metadata = {
        "source": source.name,
        "source_sha256": hashlib.sha256(raw).hexdigest(),
        "sample_rate_hz": 8_000_000,
        "start_sample": start,
        "end_sample_exclusive": end,
        "samples": len(payload),
        "duration_seconds": len(payload) / 8_000_000,
        "frames": len(cycle_frames),
        "initial_segment_word": f"{initial_segment:010X}",
        "initial_grid_word": "0000000001",
        "visible_samples": second[0] - start,
        "hidden_samples": end - second[0],
        "payload_sha256": hashlib.sha256(payload).hexdigest(),
        "bit_to_gpio": {"0": 48, "1": 47, "2": 41, "3": 42, "4": 39, "5": 40},
        "bit_to_signal": {
            "0": "FLSCLK", "1": "FLSDATA", "2": "FLCLEAR",
            "3": "FLSTB", "4": "FLGDATA", "5": "FLGCLK",
        },
        "status": "Preparation only; no firmware replay or safe stop validated",
    }
    (destination / "mono-blink.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--frequency", action="store_true")
    args = parser.parse_args()
    prepare(args.capture, args.destination, args.frequency)
