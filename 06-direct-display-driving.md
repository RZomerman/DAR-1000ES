# Direct Display driving

Bench findings: 2026-11-04, Sony DAR-1000ES.

**Status: passively decoded, not yet actively driven.** Captures at the inputs
of the two TD62C950RF display drivers reveal a repeating 26-slot scan, readable
character matrices, red-display patterns and several isolated annunciator bits.
This is the measured basis for Option 2: replacing IC701's display output with
an ESP32. No successful replay, electrical interface or replacement firmware
is claimed.

This document supersedes the **40 segment clocks + 40 grid clocks per latch**
hypothesis in [the original display chapter](02-display-control.md). The measured
steady-state sequence is **40 segment clocks + one grid clock per latch**.
The [parallel controller bus](05-bus-capture-findings.md) is a separate interface:
its byte offsets and masked values must not be confused with the slots below.

## 1. Hardware and passive capture wiring

The existing schematic identifies IC702 as the grid driver and IC703 as the
segment driver. Both are TD62C950RF 40-bit serial/high-voltage-output devices.
The analyser probes their **logic inputs**, not their high-voltage VFD outputs.

| Analyser physical channel | Software/raw bit | Sony signal |
|---------------------------|------------------|-------------|
| CH1 | D0 / bit 0 | FLSCLK |
| CH2 | D1 / bit 1 | FLSDATA |
| CH3 | D2 / bit 2 | FLCLEAR |
| CH4 | D3 / bit 3 | FLSTB |
| CH5 | D4 / bit 4 | FLGDATA |
| CH6 | D5 / bit 5 | FLGCLK |

Use a common logic ground. CH7/CH8 are unassigned. One analyser captures all
six signals, so the two-analyser drift correction used for the parallel bus
is unnecessary.

The original chapter lists DATA pin 29, CLOCK pin 34 and STB pin 26 on the
drivers, and CLEAR pin 24 on IC702. Confirm these against the actual board and
datasheet before wiring; capture channel names do not establish pin ownership
or the exact scope of FLCLEAR.

### Acquisition

The captures use sigrok-cli/fx2lafw at **8 MHz**, one byte per sample:
125 ns resolution, 40,000,000 samples / 40 MB per five-second snapshot.
The initial power-up recording was 60 seconds / 480 MB; power-on occurred
approximately 13.744 seconds into that recording.

```sh
sigrok-cli --scan
sigrok-cli --driver fx2lafw:conn=<current-usb-connection> \
  --config samplerate=8m --samples 40000000 \
  --output-format binary --output-file sony-td-state.bin
```

The connection during this session was `3.29`; it can change after reconnecting.
Ignore the initial power-up transient where multiple signals switch together.
All mappings below come from steady scans.

## 2. Measured scan timing

| Observation | Measured result |
|-------------|-----------------|
| FLSCLK rising edges between FLSTB rising edges | 40 |
| FLGCLK rising edges per latch interval | 1 |
| FLGDATA sampled HIGH | Once every 26 slots |
| Complete scan | 26 slots |
| Typical latch interval | About 301 us; observed approximately 297.125-304.875 us in steady startup decoding |
| Slot rate | About 3,322 per second |
| Full-frame refresh | About 127.76 Hz |
| FLSCLK LOW pulse | Typically 0.375-0.5 us |
| FLGCLK LOW pulse | Typically 1.875-2 us |
| FLCLEAR LOW pulse | Typically 12.375 us per slot |

The grid stream is consistent with a moving one-hot scan: inject a single
HIGH, then shift LOW on the following slots. This does **not** establish the
physical grid-output numbering, initial shift-register contents or a safe
startup/reset sequence.

FLCLEAR's observed LOW pulse is not proof of its active polarity or internal
effect. Likewise, latch rising edges are the decoder's interval boundaries,
not independent proof of the chip's active latch edge.

### Blink behavior

The setup/program snapshots contain two frame patterns. Each blink phase lasts
about **508.716 ms**, giving a full cycle of about **1.017 seconds**.
The MONO and red fields change between phases while PROGRAM selection remains
present. The later primary No DSR snapshots are steady, with one frame pattern.

## 3. Decoder conventions and validation

All slot numbers are **zero-based, decoder-relative**, not physical grid pins.
Slot 0 is the latch record whose preceding interval contains FLGDATA=1 at the
grid-clock rising edge. Consecutive such records must be exactly 26 slots apart.

For each interval between consecutive FLSTB rising edges:

1. Find the FLSCLK rising edges and sample FLSDATA at each.
2. Accumulate in transmission order: `word = (word << 1) | data_bit`.
3. Store the result as a 40-bit, ten-hex-digit word.
4. Sample FLGDATA at the FLGCLK rising edge to locate the frame marker.

Accept a complete frame only when all 26 records have **40 segment clocks and
one grid clock**. This detects malformed acquisition intervals rather than
treating overlong or truncated words as display changes.

Rising-edge sampling produces recognizable glyphs. Falling-edge decoding was
also examined and differs for some words. The precise sampling edge, setup/hold
margins and serial-bit-to-output-pin mapping still require datasheet and timing
verification before driving hardware.

**Compare matching blink phases.** In particular, changing a large red glyph
can make nearest-Hamming-distance matching choose the wrong phase. Use the
unchanged annunciator context (such as slot 24) to pair visible with visible
and hidden with hidden.

One LNC A acquisition was badly malformed: no complete frame passed the above
checks. Its clean repeat contained 634 identical valid frames and is the
evidence used below; the corrupted attempt is not evidence of Sony behavior.

## 4. Character matrices and slot layout

Text glyphs are recognizable in the low 35 bits of each 40-bit word. For the
observed text, reverse those 35 bits and split into seven rows of five pixels:

```python
pixels = f"{word:040b}"[5:][::-1]
rows = [pixels[i:i + 5] for i in range(0, 35, 5)]
print("\n".join(row.replace("0", ".").replace("1", "#") for row in rows))
```

This is a rendering convention for captured words, not a complete font or
proven physical segment assignment. Do not apply it to the annunciator/red
words as though those were ordinary 5x7 characters.

| Slot(s) | Observed use |
|---------|--------------|
| 0 | `0000004000` in the primary No DSR states; exact indicator assignment unresolved |
| 1-2 | Zero in the documented states |
| 3-5 | `z`, `H`, `M`: reverse scan order for MHz |
| 6-10 | Frequency, right to left; decimal point occupies a slot |
| 11-13 | Zero in the documented states |
| 14-22 | Nine-character message, right to left |
| 23 | Source and LNC annunciators |
| 24 | PROGRAM and MONO annunciators, plus unresolved bits |
| 25 | Red letter/digit display, plus unresolved bits |

### Complete text examples

Read each sequence below in increasing slot order; screen reading order is
the reverse.

| Slots | Screen | Captured words in slot order |
|-------|--------|-----------------------------|
| 3-5 | MHz | `07C4447C00 04631FC631 04631AD771` |
| 6-10 | 132.0 | `03A318C62E 0080000000 07C226422E 03A306422E 02108421C8` |
| 6-10 | 1227 | `010844221F 07C226422E 07C226422E 02108421C8 0000000000` |
| 14-22 | Frequency | `03A1E8C400 078210F800 046319B400 0783F8B800 05B318C400 043D18F800 0783F8B800 0084236800 004217843F` |
| 14-22 | No DSR, with leading screen blanks | `045257C62F 03A307062E 03E5294A4F 0000000000 03A318B800 04639ACE31 0000000000 0000000000 0000000000` |

The two `e` characters in Frequency have the same word. These are measured
examples, not an exhaustive alphabet or numeric font.

## 5. Source and LNC: slot 23

| Indicator | Mask | Evidence |
|-----------|------|----------|
| CABLE | `0x01` | Primary CABLE/OFF versus SATELLITE/OFF |
| SATELLITE | `0x02` | Same comparison |
| LNC label candidate | `0x04` | Common to OFF/A/B states reported with LNC; not independently switched |
| LNC OFF | `0x08` | OFF to A clears this bit |
| LNC A | `0x10` | OFF to A sets this bit |
| LNC B | `0x20` | A to B clears A and sets this bit |

| Observed state | Full slot-23 word |
|----------------|-------------------|
| CABLE in the earlier Frequency/setup snapshots | `0000000001` |
| SATELLITE, LNC OFF | `000000000E` |
| SATELLITE, LNC A | `0000000016` |
| SATELLITE, LNC B | `0000000026` |
| CABLE, LNC OFF in the final primary snapshot | `000000000D` |

OFF -> A changed only slot 23 (`0x0E -> 0x16`); A -> B also changed only that
slot (`0x16 -> 0x26`). The primary SATELLITE/OFF -> CABLE/OFF comparison gives
`0x0E -> 0x0D`, isolating the source pair while retaining LNC/OFF.

## 6. PROGRAM and MONO: slot 24

| Indicator | Mask | Status |
|-----------|------|--------|
| PROGRAM 1 | `0x80` | Isolated by PROGRAM 1 -> 2 -> 3 |
| PROGRAM 2 | `0x40` | Isolated |
| PROGRAM 3 | `0x20` | Isolated |
| Left MONO index 1 | `0x08` | Left index 1 -> 2 clears this bit |
| Left MONO index 2 | `0x04` | Left index 1 -> 2 sets this bit |
| Right-hand MONO indicator | `0x800` | Clears when the right-hand MONO is removed |
| Left MONO label candidate | `0x10` | Remains with either left index during the visible phase; not independently isolated |
| Unassigned setup-state bit | `0x100` | Present in these setup snapshots, absent in primary No DSR |

**Naming correction:** `0x800` was initially called "MONO 2". The user's
clarification distinguishes the **left MONO with index 1/2** from the separate
**right-hand MONO**. `0x800` belongs to the right-hand indicator, not the left
digit 2.

| State | Visible phase | Hidden phase |
|-------|---------------|--------------|
| PROGRAM 1, left MONO 1 and right MONO | `0000000998` | `0000000180` |
| PROGRAM 2, same MONO state | `0000000958` | `0000000140` |
| PROGRAM 3, same MONO state | `0000000938` | `0000000120` |
| PROGRAM 3, only left MONO 1 | `0000000138` | `0000000120` |
| PROGRAM 3, only left MONO 2 | `0000000134` | `0000000120` |
| Primary No DSR states | `0000000000` (steady) | Not blinking |

Removing the right-hand MONO changes only `0x800` in the visible phase.
Changing the left index changes only `0x0C` (clear `0x08`, set `0x04`).
Neither operation changes the hidden phase or any other slot.
MEMORY was reported on the setup screen, but no dedicated MEMORY-only
transition has isolated its bit. Do not label `0x100` as MEMORY yet.

## 7. Red display: slot 25

These are complete measured words, including any coexisting indicators.
Individual segment masks are not yet fully assigned.

| User-reported red display/context | Slot-25 word |
|----------------------------------|--------------|
| Dashes `--`, setup visible phase | `0000800022` |
| A, setup visible phase | `0000478C22` |
| A2, setup visible phase | `0000478D6E` |
| A9, setup visible phase | `0000478DBE` |
| D9, after source change, setup visible phase | `0000A41DBE` |
| Hidden phase of the above setup states | `0000000002` |
| D alone, primary No DSR, steady | `0000A41C00` |
| B alone, primary No DSR, steady | `0000A61C00` |

A -> A2 has XOR `0x14C`; A2 -> A9 has XOR `0xD0`.
The source-change snapshot was later explicitly confirmed as **D9**, not A9:
the digit 9 remains while the letter changes. D -> B in the primary snapshots
has XOR `0x20000`. These deltas are not complete encodings for arbitrary
letters/digits, and the setup and primary words should not be mixed without
accounting for their other bits.

## 8. Snapshot evidence

The capture prefix identifies the local `.bin`, `-decoded.json` and
`-analysis.txt` artifacts. Counts below include only structurally valid,
complete frames from five one-second decoding windows.

| Capture prefix (all start `sony-td-`) | Valid frames | Patterns | Principal observation |
|--------------------------------------|--------------|----------|-----------------------|
| `mono-blink` | 634 | 2 | Frequency 132.0, PROGRAM 1, red dashes, CABLE; MEMORY reported |
| `program2` | 634 | 2 | PROGRAM 2 only |
| `program3` | 633 | 2 | PROGRAM 3 only |
| `red-a` | 633 | 2 | Red A |
| `red-a2` | 634 | 2 | Red A2 |
| `red-a9` | 634 | 2 | Red A9 |
| `satellite-lnc-off` | 633 | 2 | SATELLITE/LNC/OFF, 1227 MHz, red D9 |
| `mono1-only` | 634 | 2 | Right MONO removed; left index 1 remains |
| `left-mono2` | 633 | 2 | Left index 2, right MONO still off |
| `primary-no-dsr` | 634 | 1 | D, No DSR, 1227 MHz, SATELLITE, LNC OFF, antenna bars |
| `primary-lnc-a-retry` | 634 | 1 | LNC A only |
| `primary-lnc-b` | 634 | 1 | LNC B only |
| `primary-b-cable-lnc-off` | 633 | 1 | B, No DSR, 132.0 MHz, CABLE, LNC OFF, antenna bars |

The original power-up capture (`sony-td-direct-startup`) established scan timing
and the initial D/No DSR/1227 MHz/SATELLITE/LNC/OFF screen.
Large raw recordings and session decoder artifacts are retained locally,
not committed to this repository; the tables preserve the decoded findings.

In the final primary comparison, frequency slots 6/7/9/10, source slot 23 and
red slot 25 change. No DSR, slot 0 (`0000004000`) and the remaining slots do not.
The user reported antenna-strength dots/bars, but no controlled strength sweep
or individual bar transition was captured: neither the bar count nor its
segment map is established.

## 9. What remains before direct driving

The measured model suggests preparing 26 segment words, shifting 40 segment
bits and one grid bit per slot, and reproducing the measured latch/blanking
sequence. This is a design direction, **not a validated replay recipe**.

Before any active experiment:

1. Verify driver datasheets, logic levels, sampling/latch edges, bit order,
   FLCLEAR behavior, setup/hold and power-up initialization.
2. Isolate IC701's outputs before connecting another push-pull driver; never
   actively oppose the original controller.
3. Provide a verified electrical interface. ESP32 GPIO is not 5 V tolerant;
   do not assume 3.3 V meets the Sony drivers' HIGH threshold.
4. Establish safe blanking and grid sequencing so a stalled scan cannot leave
   a grid continuously energized. Preserve the existing VFD supply/filament
   arrangements and keep probes off high-voltage outputs.
5. Isolate MEMORY, the left MONO label, LNC label, antenna bars and remaining
   indicators; expand red character and text font coverage.
6. Measure brightness/dimming and startup/shutdown behavior. None is mapped
   sufficiently for a full replacement.

The current result is a reproducible passive display map with specific
confirmed transitions, not a finished ESP32 driver.
