# ESP32 direct display-driver specification

Consolidated capture evidence through 2026-11-08, with optical mapping
updated on 2026-11-13. Detailed capture history and
datasheet comparisons: [Direct Display driving](06-direct-display-driving.md).

## Current optical mapping: 2026-11-13

The complete camera inventory tested all **26 slots x 40 one-hot bits**:
846 commands produced resolved optical responses; 194 remain unresolved,
not proven unused. Comparison with the FL701 service drawing found
**no unmatched functional reference symbol**.

| Slot | Camera-confirmed assignment |
|------|-----------------------------|
| 0 | Bits 0-13: signal bars 1-14; bit 14: all 14 bottom dots together |
| 1-22 | Bits 0-34: 5x7 dot matrices; bit = row * 5 + column, with top-left origin; slots increase right-to-left |
| 23 | Bits 0-5: CABLE, SATELLITE, LNC, OFF, A, B; bits 6-21: lower markers 1-16 |
| 24 | Bits 0-14: audio, PROGRAM and tuning symbols; `0x100` is PROGRAM, not MEMORY |
| 25 | Bits 0-1: STEREO/MEMORY; bits 2-8: digit segments a-g; bit 9: adjacent "1"; bits 10-23: left red glyph strokes |

Use the [complete command map](media/display-command-map.json) and
[per-bit optical atlas](media/display-command-atlas.svg) for exact identities,
words and observed shapes. The
[annotated reference comparison](media/display-reference-coverage.svg) and
[coverage report](media/display-reference-coverage.json) record 861 defined
comparison entries linked to those 846 commands. Whole legends count once;
coupled meter dots, digit strokes and right MONO/index 2 count separately.
All 770 reference matrix cells have unique optical matches.

Sections 5-7 below are the current element-control guide, replacing the
earlier candidate/unknown physical assignments. The optical inventory
does not supersede electrical constraints.
The inventory used conservative held-grid CLEAR PWM, with 560 commands
revalidated at 3.75% duty and the remaining 480 captured at that duty.
The reference comparison used existing sequential evidence, not simultaneous
all-elements illumination. Coverage is by functional identity and topology,
not exact phosphor boundaries, uniform brightness, absence of faint ghosting
or electrical/current-rating certification.

## 1. Scope and evidence status

This specifies direct control of the IC702 grid driver and IC703 segment
driver, both Toshiba TD62C950RF. It is **not** the parallel command protocol
between the original controllers.

The display has no high-level "show MEMORY" bus command. A complete screen
is represented by 26 slot words, each 40 bits wide, refreshed through grid
multiplexing. For example, the confirmed MEMORY control is **slot 25 mask
`0x02`**, not a standalone bus command. The same mask in another slot has a
different meaning. Held-grid tests isolate one slot under limited CLEAR
duty; they do not establish reliable full-screen multiplexing.

| Status | Meaning |
|--------|---------|
| Optically confirmed | An isolated command produced the identified visible response in controlled camera tests |
| Datasheet-defined | Specified chip behavior; actual interface compliance still requires measurement |
| Captured | A complete observed pattern; individual physical elements may not be isolated |
| Derived | Composed from known pixels/segments; not necessarily an original Sony glyph or a visually checked combination |
| Unresolved | No resolved optical response, or a separately identified outstanding electrical/behavioral question |

Individual fixed-element controls and all 770 matrix-dot positions are now
optically confirmed. Distinguish that mapping from **electrical verification
and reliable multiplexing**, which are not established by knowing the bits.
A missing original Sony glyph does not prevent constructing a character
from the confirmed 5x7 pixel layout; its design/readability can still need review.

### Capability inventory

| Screen element | What we have | What is still unresolved |
|----------------|--------------|--------------------------|
| All 22 text matrices | Every 5x7 dot and its orientation confirmed, including slots formerly called unassigned | Complete original Sony font and readability of every generated glyph are not established |
| PROGRAM and indices 1/2/3 | Four separate controls: bits 8, 7, 6, 5 of slot 24 | No mapping gap |
| Left audio legends and MONO indices | SPEECH, MUSIC, MONO, 1 and 2 independently mapped | No mapping gap |
| Right audio legends | SPEECH and MUSIC independent; MONO plus its 2 share one bit | MONO and its 2 cannot be separated by the mapped command |
| STEREO and MEMORY | Separate slot-25 bits 0 and 1, optically confirmed | No mapping gap |
| Left red glyph | All 14 stroke controls mapped, including faint bit 16 | New letter combinations are derived designs until visually reviewed |
| Red numeric field | Seven independent right-digit segments and a separate adjacent 1-shaped control | Adjacent 1 has two strokes driven together; it is not another full seven-segment digit |
| Signal meter | 14 bars in increasing order; 14 bottom dots share bit 14 | Bottom dots cannot be independently selected by the mapped command |
| Source/LNC and lower markers | All six legends and all 16 numbered pads mapped | No mapping gap |
| Brightness, hide and blink | Original timing/data captures and controlled held-grid PWM evidence | Reliable full-screen scanning and electrical margins remain separate validation work |
| Upper/unresolved bits | All 194 were tested without a resolved response | Not proven unused; keep zero in normal compositions |

Historical firmware 0.3.7 tests replayed captured waveforms. Readable live
Frequency output was observed, but reliable live operation was unresolved. Simultaneous
STB measurements showed a clean ESP input to the buffer and extra short pulses
at its output. Buffer, wiring and analyser/probe causes are not fully separated.
The later inventory used firmware 0.3.23 held GPIO preload with CLEAR-only
DMA. A timing-adjustable experimental driver exists, but clean, stable full
26-slot multiplexing is still unresolved; confirmed mapping does not resolve it.

## 2. Electrical interface and signal assignments

The following GPIO/analyser table documents the earlier replay captures,
not the current live test wiring. Verify the selected firmware profile and
physical connections before driving. Slot/bit identities do not depend on
which ESP GPIOs carry the serial signals.

| Signal | Purpose | ESP GPIO | Current analyser channel | Screen connector pin |
|--------|---------|----------|--------------------------|----------------------|
| FLGDATA | Grid serial DATA | 39 | CH1 | 1 |
| FLGCLK | Grid shift clock | 40 | CH2 | 2 |
| FLCLEAR | Grid output blanking, active LOW | 41 | CH3 | 3 |
| FLSTB | Shared latch control | 42 | CH4 | 4 |
| FLSDATA | Segment serial DATA | 47 | CH5 | 5 |
| FLSCLK | Segment shift clock | 48 | CH6 | 6 |

Original fixture raw bits are in a different order:
bit 0 FLSCLK, bit 1 FLSDATA, bit 2 FLCLEAR, bit 3 FLSTB,
bit 4 FLGDATA, bit 5 FLGCLK. Remap current capture source bits
`[5, 4, 2, 3, 0, 1]` to reproduce that order.

| Requirement | Specification / remaining uncertainty |
|-------------|---------------------------------------|
| Logic supply | Verify driver VDD; the current interface uses 5 V |
| Input HIGH | At VDD=5 V, TD62 minimum VIH is 3.5 V |
| Input LOW | At VDD=5 V, TD62 maximum VIL is 1.5 V |
| ESP conversion | Use verified 3.3 V to 5 V push-pull buffering; direct 3.3 V is not guaranteed |
| Buffer ownership | Disconnect IC701 display outputs before the ESP drives; never oppose two push-pull outputs |
| 74HCT245 modules | Set a fixed ESP-to-Sony direction; control active-LOW OE; verify board pinout and supply |
| Ground / supply | Common logic ground and local buffer decoupling are required |
| Other TD62 inputs | Segment CL and both CHG connections have not been independently verified |
| GPIO48 | Shared with the ESP board's RGB LED; its color does not diagnose display correctness |
| Probe safety | Logic inputs only; do not probe high-voltage VFD outputs with these analysers |

Manufacturer top-view TD62 pins: DATA/S-in 32, CK 27, STB 35, CL 37,
CHG 25, S-out 29, VDD 30/31. Earlier traced numbers used reversed counting
(`61 - manufacturer pin`); they must not be used as literal manufacturer pins.

## 3. Serial and latch semantics

| Signal / operation | Confirmed behavior |
|--------------------|--------------------|
| CK rising | Shift serial DATA into the 40-bit shift register |
| STB HIGH | Output latch is transparent to the shift register |
| STB LOW | Output latch holds its previous value |
| STB falling | Closes the transparent latch; not a STB-rising edge-triggered latch |
| Grid CL LOW | Forces grid outputs LOW; does not reset the shift register |
| CHG HIGH, CL HIGH | Forces outputs HIGH according to datasheet truth table/chart |
| Segment word | Captures accumulate bits in transmission order: `word = (word << 1) | data` |
| Word transmission | Send bits 39 through 0 to reproduce the recorded word |
| Grid scan | One HIGH marker followed by LOW shifts; one grid clock per normal slot |

Slot numbering is **decoder-relative**, not physical grid pin numbering.
Slot 0 is the latch record whose preceding grid shift sampled FLGDATA HIGH.
A complete frame contains 26 consecutive slots, each with 40 segment clocks
and one grid clock. Shifts after a latch window prepare the **next** window.
The marker-to-physical-grid assignment still requires live verification.

## 4. Timing, brightness and blinking

| Parameter | Measured original behavior |
|-----------|----------------------------|
| Normal slot duration | About 301 us |
| Frame duration / rate | About 7.827 ms / 127.76 Hz |
| Segment CK LOW/HIGH minimum | 375 / 375 ns in the compared Frequency fixture |
| Grid CK LOW | About 1.875-2 us |
| DATA setup / hold, segment | Measured minima 375 / 375 ns |
| STB HIGH | 2.75-2.875 us |
| Blink phase | About 509 ms, typically 65 frames |
| Full blink cycle | About 1.017 s |

With ordinary STB rising at time zero:

| Event | Typical relative time / action |
|-------|--------------------------------|
| Grid CL | Already LOW before STB rises |
| STB falls | +2.75 to +2.875 us |
| Grid CK falls / rises | About +4.75 / +6.625 us |
| CL rises, bright mode | About +10.375 to +10.5 us |
| Segment shifting starts | About +20.875 to +21 us |
| CL falls | Before the next latch update |

| Brightness mode | Measured grid CL LOW per slot | Measured CL HIGH fraction |
|-----------------|-------------------------------|--------------------------|
| Bright | 12.375-12.5 us | 95.89% |
| Dim | Typically about 202.875 us | 32.59% |

Brightness is controlled by grid blanking duration, not an identified segment
data bit. These are electrical duty fractions, not perceived-brightness values.
The exact placement of the dim-mode blanking edges relative to STB remains
to be extracted before generating that mode; its total LOW duration is measured.
Indicator hiding is separate: the DISPLAY capture cleared slots 0 and 23
without changing the text.

Use page 5's recommended operating limits for driver settings:
VDD=4.5-5.5 V, CK <=8 MHz, CK pulses >=75 ns and DATA setup/hold >=50 ns.
Page 7's 25 C switching-test figures (CK/STB 70 ns, DATA 20 ns,
CK-to-STB 50 ns, f(max) minimum 9 MHz) are separate characterization
conditions, not permission to relax the recommended limits.
An 8 MHz analyser has 125 ns resolution and cannot certify analogue
margins or threshold crossings. See [chapter 12](12-td62c950rf-timing-revalidation.md#datasheet-limits).

## 5. Screen slot map

All assignments below are **optically confirmed**. Slot numbers are
zero-based and decoder-relative, not physical TD62 grid-pin numbers.

| Slot | Physical field | Resolved bits | Bits with no resolved response |
|------|----------------|---------------|--------------------------------|
| 0 | Signal bars and bottom dots | 0-14 | 15-39 |
| 1-22 | 22 generic 5x7 matrices | 0-34 in each slot | 35-39 in each slot |
| 23 | Source/LNC labels and lower markers 1-16 | 0-21 | 22-39 |
| 24 | Audio, PROGRAM and tuning symbols | 0-14 | 15-39 |
| 25 | STEREO, MEMORY and red segments | 0-23 | 24-39 |

Slots 1-2 and 11-13 are matrices too; they were merely blank in earlier
captures. The 194 unresolved bits are not established spare pixels or
control flags. Leave them zero in normal compositions.

## 6. Fixed-element control guide

Every listed bit-to-element assignment is optically confirmed, rather than
a candidate inferred from composite words. Masks are hexadecimal within the
named slot. OR independent masks to combine elements; clear a mask to hide
that element. To replace one field while preserving the others:

```c
word = (word & ~field_mask) | (new_bits & field_mask);
```

Use unsigned 64-bit storage for each 40-bit word. Initialize a new frame's
26 words to zero; changing data must not itself authorize or start output.
For raw API words, pad hexadecimal values to ten characters:
`0x180` becomes `"0000000180"`.

### Slot 0: signal strength

| Bit(s) | Mask / formula | Visible element |
|--------|----------------|-----------------|
| 0-13, individually | `1ULL << (n - 1)`, bar `n = 1..14` | One bar, from the smallest/leftmost to largest/rightmost |
| 14 | `0x4000` | All 14 bottom dots together |

For a cumulative level of `n = 0..14` bars, use `(1ULL << n) - 1`.
Examples: one bar `0x01`, two `0x03`, three `0x07`, all fourteen
`0x3FFF`. OR `0x4000` to add the dots; bars plus dots are `0x7FFF`.
The bar field mask is `0x3FFF`; dots are independent of that field,
but individual bottom dots cannot be selected separately.

### Slot 23: source/LNC and lower numbered markers

| Bit | Mask | Visible element |
|-----|------|-----------------|
| 0 | `0x01` | CABLE |
| 1 | `0x02` | SATELLITE |
| 2 | `0x04` | LNC |
| 3 | `0x08` | OFF |
| 4 | `0x10` | A |
| 5 | `0x20` | B |

LNC is its own label, separate from OFF, A and B. For example,
CABLE + LNC + OFF is `0x0D`; SATELLITE + LNC + A is `0x16`.
These six labels occupy mask `0x3F`.

Each lower marker is an illuminated pad containing its dark numeral:

| Marker | Bit | Mask | Marker | Bit | Mask |
|--------|-----|------|--------|-----|------|
| 1 | 6 | `0x40` | 9 | 14 | `0x4000` |
| 2 | 7 | `0x80` | 10 | 15 | `0x8000` |
| 3 | 8 | `0x100` | 11 | 16 | `0x10000` |
| 4 | 9 | `0x200` | 12 | 17 | `0x20000` |
| 5 | 10 | `0x400` | 13 | 18 | `0x40000` |
| 6 | 11 | `0x800` | 14 | 19 | `0x80000` |
| 7 | 12 | `0x1000` | 15 | 20 | `0x100000` |
| 8 | 13 | `0x2000` | 16 | 21 | `0x200000` |

For marker `n = 1..16`, use `1ULL << (n + 5)`. The marker field mask
is `0x3FFFC0`; preserve the source/LNC bits when replacing it.
For example, CABLE + LNC + OFF + marker 1 is `0x4D`.

### Slot 24: audio, PROGRAM and tuning

| Bit | Mask | Visible element |
|-----|------|-----------------|
| 0 | `0x01` | Left SPEECH |
| 1 | `0x02` | Left MUSIC |
| 2 | `0x04` | Left MONO index 2 only |
| 3 | `0x08` | Left MONO index 1 only |
| 4 | `0x10` | Left MONO label only |
| 5 | `0x20` | PROGRAM index 3 only |
| 6 | `0x40` | PROGRAM index 2 only |
| 7 | `0x80` | PROGRAM index 1 only |
| 8 | `0x100` | PROGRAM label only |
| 9 | `0x200` | Right SPEECH |
| 10 | `0x400` | Right MUSIC |
| 11 | `0x800` | Right MONO and its index 2 together |
| 12 | `0x1000` | Right-pointing tuning triangle |
| 13 | `0x2000` | TUNED |
| 14 | `0x4000` | Left-pointing tuning triangle |

**PROGRAM, 1, 2 and 3 are four separate controls.** PROGRAM + 1 is
`0x180`; PROGRAM + 2 is `0x140`; PROGRAM + 3 is `0x120`.
Replace those four controls with field mask `0x1E0` without touching audio
or tuning symbols. PROGRAM is not MEMORY; MEMORY is in slot 25.

Left MONO + 1 is `0x18`; left MONO + 2 is `0x14`.
The separate right MONO/index-2 group is `0x800`; its two symbols are
not independently controllable by the mapped command.
Both tuning triangles plus TUNED are `0x7000`.

### Slot 25: status labels and individual red segments

| Bit | Mask | Visible element |
|-----|------|-----------------|
| 0 | `0x01` | STEREO |
| 1 | `0x02` | MEMORY |
| 2 | `0x04` | Right digit a: top |
| 3 | `0x08` | Right digit b: upper-right |
| 4 | `0x10` | Right digit c: lower-right |
| 5 | `0x20` | Right digit d: bottom |
| 6 | `0x40` | Right digit e: lower-left |
| 7 | `0x80` | Right digit f: upper-left |
| 8 | `0x100` | Right digit g: centre |
| 9 | `0x200` | Adjacent larger 1: both vertical strokes together |
| 10 | `0x400` | Left glyph top horizontal |
| 11 | `0x800` | Left glyph upper-right outer vertical |
| 12 | `0x1000` | Left glyph upper-centre vertical |
| 13 | `0x2000` | Left glyph upper-left diagonal |
| 14 | `0x4000` | Left glyph upper-right diagonal |
| 15 | `0x8000` | Left glyph upper-left outer vertical |
| 16 | `0x10000` | Left glyph centre-left short element |
| 17 | `0x20000` | Left glyph centre-right short element |
| 18 | `0x40000` | Left glyph lower-right outer vertical |
| 19 | `0x80000` | Left glyph lower-right diagonal |
| 20 | `0x100000` | Left glyph lower-left diagonal |
| 21 | `0x200000` | Left glyph lower-centre vertical |
| 22 | `0x400000` | Left glyph lower-left outer vertical |
| 23 | `0x800000` | Left glyph bottom horizontal |

Standard seven-segment positions, with slot-25 bit numbers:

```text
       2 (a)
 7 (f)       3 (b)
       8 (g)
 6 (e)       4 (c)
       5 (d)
```

The right digit has seven independent segments. The adjacent position is
only a 1-shaped element, not a second full digit; its upper and lower
strokes share bit 9. The 14 left-glyph controls are separate from both.
Bit 16 was confirmed using the controlled, more sensitive camera follow-up.

The following standard digit words are **derived combinations of confirmed
segments**, not a claim that each combination received a separate live test:

| Digit | Segments | Slot-25 numeric word |
|-------|----------|----------------------|
| 0 | abcdef | `0x0FC` |
| 1 | bc | `0x018` |
| 2 | abdeg | `0x16C` |
| 3 | abcdg | `0x13C` |
| 4 | bcfg | `0x198` |
| 5 | acdfg | `0x1B4` |
| 6 | acdefg | `0x1F4` |
| 7 | abc | `0x01C` |
| 8 | abcdefg | `0x1FC` |
| 9 | abcdfg | `0x1BC` |

OR `0x200` for the adjacent 1; for example, the numeric field for 16 is
`0x3F4`. Use field mask `0x3FC` when replacing the whole numeric field,
`0x1FC` for only the right digit, and `0xFFFC00` for the left glyph.
Preserve STEREO/MEMORY when changing either red field.
Additional red letters can be constructed from the stroke guide; original
Sony composite words and capture history remain in
[chapter 6](06-direct-display-driving.md#7-red-display-slot-25).

## 7. Generic 5x7 matrix guide

All slots **1-22** have the same optically confirmed 35-dot layout.
Rows run top to bottom and columns left to right, both zero-based:

```text
 0  1  2  3  4
 5  6  7  8  9
10 11 12 13 14
15 16 17 18 19
20 21 22 23 24
25 26 27 28 29
30 31 32 33 34
```

Set a dot with `1ULL << (row * 5 + column)` for `row = 0..6`,
`column = 0..4`. OR dot masks to form any bitmap. The complete matrix
mask is `0x07FFFFFFFF`; bits 35-39 remain zero.
Examples: top-left `0x0000000001`, top-right `0x0000000010`,
bottom-left `0x0040000000`, bottom-right `0x0400000000`.
Transmit all 40 bits, bit 39 first; pixel numbering is not wire order.

The 22 physical cells run **slot 22 at the left to slot 1 at the right**.
For zero-based full-width screen column `i = 0..21`, use slot `22 - i`.
The legacy Sony layout assigns slots 22 down to 14 to the nine-character
message, slots 10 down to 6 to frequency, and slots 5 down to 3 to the MHz
suffix. The other cells remain usable matrices. A message-only API must
still preserve the other fields rather than overwrite all 22 slots.

For a row-major bitmap, set each `#` at its corresponding bit. To decode
an existing captured word for display:

```python
pixels = f"{word:040b}"[5:][::-1]
rows = [pixels[i:i + 5] for i in range(0, 35, 5)]
```

The physical orientation and slot order are confirmed; they no longer
require inference from whole-character captures. This convention does not
apply to fixed annunciators or slot-25 red segments.

### Original Sony glyph examples

| Field | Captured words in increasing slot order |
|-------|-----------------------------------------|
| MHz, slots 3-5 | `07C4447C00 04631FC631 04631AD771` |
| 132.0, slots 6-10 | `03A318C62E 0080000000 07C226422E 03A306422E 02108421C8` |
| 118.0, slots 6-10 | `03A318C62E 0080000000 03A317462E 02108421C8 02108421C8` |
| 1227, slots 6-10 | `010844221F 07C226422E 07C226422E 02108421C8 0000000000` |
| Frequency, slots 14-22 | `03A1E8C400 078210F800 046319B400 0783F8B800 05B318C400 043D18F800 0783F8B800 0084236800 004217843F` |
| No DSR, slots 14-22 | `045257C62F 03A307062E 03E5294A4F 0000000000 03A318B800 04639ACE31 0000000000 0000000000 0000000000` |

The 5x7 layout supports constructing all text letters and digits using an
application font; we do not need another capture for every desired character.
The captured examples are not an exhaustive catalogue of the **original Sony
font**. Remaining original glyph words and the readability of generated
designs can still need review; the pixel orientation is known. The upper
five bits produced no resolved response and must not be treated as flags.

### Music note and extended song-title character requirements

The close-up reference gives this **photo-derived, 14-pixel** music note:

```text
..#..
..##.
..#.#
..#.#
.##..
###..
##...
```

Its complete 40-bit word is **`0x00CE6A5184`**, using
`bit = row * 5 + column`, with rows counted top to bottom and columns left
to right. Bits 35-39 are zero; transmit bit 39 first. This replaces the
earlier speculative `0x00CE425184`. It is not a Sony bus capture and has
not been validated on the physical display. Use only mapped text slots,
not fixed annunciators or the red slot-25 segment group.

The application font must support song-title and artist-name characters
beyond ASCII. The catalogue and local editor now include generated records
for the following categories; this update is **not yet deployed to the ESP**
and does not establish original Sony shapes:

| Category | Examples / intended coverage |
|----------|------------------------------|
| Music symbols | Music note; distinct single/double-note designs where the grid permits |
| Title punctuation | Apostrophes, quotation marks, ampersand, parentheses, hyphen, en/em dash, ellipsis |
| Accented letters | Uppercase/lowercase vowels with acute, grave, circumflex, tilde and diaeresis |
| Other Latin letters | Cedilla, slashed letters, sharp s and ligatures |
| Additional scripts/symbols | Explicitly catalogued 5x7 approximations or documented fallbacks |

Each added glyph must record its Unicode code point(s), name, seven bitmap
rows, complete 40-bit HEX word and provenance: captured, photo-derived or
generated. Preserve captured Sony glyphs when available. Validate bitmap/HEX
agreement and flag designs not yet checked on hardware.

Normalize canonically equivalent composed/decomposed Unicode forms before
lookup. Define a transliteration or visible replacement for unsupported
characters; do not silently discard them. Multi-code-point symbols need an
explicit single-cell approximation or fallback. The 5x7 grid limits accent
detail, and this note design is not a verified encoding of every Unicode
musical-note character.

The local editor normalizes NFC and segments grapheme clusters before slot
placement. Unlisted decomposable Latin accents transliterate to the base
letter; other unsupported graphemes, including emoji ZWJ sequences, use one
visible asterisk (U+002A) each. A trailing presentation selector on a listed
symbol uses the same monochrome glyph. The current repertoire is finite,
not complete Unicode coverage; all new shapes still need live validation.

Inbound UTF-8 is decoded to Unicode text at the transport boundary, never
treated as one display character per byte. The browser editor receives decoded
strings. Invalid UTF-8 must be rejected or replaced with U+FFFD; U+FFFD maps to
the asterisk. Lookup order is captured Sony glyph, custom Unicode glyph,
existing application font, decomposable Latin base-letter transliteration,
then the asterisk. Transliteration reuses the same lookup, preserving captured
Sony base letters. Unsupported Chinese characters therefore show one `*` per
grapheme. Literal question marks remain question marks.

The asterisk has its own generated 5x7 record and HEX value in the catalogue.
This is UTF-8 input mapping with finite glyph coverage, not a claim that all
Unicode characters are readable on 35 pixels. A future broad-font approximation
can precede the final asterisk without overriding Sony/custom glyphs; that
external font conversion is not currently implemented.

See the [music-note bitmap](09-music-note-bitmap.md) and
[complete bitmap/HEX catalogue](10-display-bitmap-catalogue.md).

## 8. Captured actions and display outcomes

| Action / capture | Data outcome | Visible interpretation / limit |
|------------------|--------------|--------------------------------|
| PROGRAM 1 to 2 to 3 | Slot 24 selects `0x80`, `0x40`, `0x20` | PROGRAM selection |
| Remove right-hand MONO | Slot 24 clears `0x800` in visible phase | Separate right MONO removed |
| Left MONO 1 to 2 | Slot 24 clears `0x08`, sets `0x04` | Left index changes |
| SATELLITE to CABLE, LNC OFF retained | Slot 23 `0x0E` to `0x0D` | Source changes |
| LNC OFF to A | Slot 23 `0x0E` to `0x16` | OFF clears, A sets |
| LNC A to B | Slot 23 `0x16` to `0x26` | A clears, B sets |
| MEMORY activation, earlier B and recent A captures | Slot 25 adds `0x02`; red character alternates visible/hidden | MEMORY capture correlation plus red blinking |
| Press numeric 1 after MEMORY | No separately established effect | User saw no change; key detection was not captured |
| DISPLAY brightness change | Grid CL LOW duration changes between about 12.4 and 203 us | Bright/dim without required text changes |
| DISPLAY indicator hide | Slot 0 and slot 23 become zero | Signal indicator and source/LNC hidden |
| Startup signal transition | Slot 0 `0x4000` to `0x7FFF` | Reported lower-dot state to full signal indicator |
| A16 blinking setup | Slot 24 `0x998` / `0x180`; slot 25 `0x478FF6` / `0x02` | MONO/red phases, PROGRAM retained |
| Physically "mangled" display capture | 634 identical valid No DSR frames | Commands decode normally; visual/electrical correctness not established |

## 9. Complete recent screen snapshots

Unless listed, slots 1-2 and 11-13 were zero.

| Captured screen | Slot 0 | Slot 23 | Slot 24 | Slot 25 | Behavior |
|-----------------|--------|---------|---------|---------|----------|
| Frequency 118.0, CABLE, PROGRAM 1, reported MONO 1/MONO 2 and `1` | `0x00` | `0x01` | `0x998` | `0x18` | Steady |
| Same text/source, red A16 | `0x00` | `0x01` | `0x998` / `0x180` | `0x478FF6` / `0x02` | About 509 ms phases |
| D, No DSR, 1227 MHz, SATELLITE/LNC OFF, full bars | `0x7FFF` | `0x0E` | `0x00` | `0xA41C00` | Steady |
| Original startup Frequency 50.0 MHz, CABLE/LNC OFF | `0x4000`, then `0x7FFF` | `0x0D` | `0x00` | `0x00` | Bar fills about 0.986 s after text appears |

The first `1` snapshot does not resolve both reported `1` indications.
Visual observations must not be silently replaced by guessed bit labels.

## 10. Driver implementation contract

| Area | Required behavior |
|------|-------------------|
| Frame representation | 26 unsigned 40-bit segment words plus brightness/blink state |
| Mapping | Use the confirmed guides in sections 5-7; keep unresolved bits zero and slot-specific masks separate |
| Composition | Modify only intended bits; preserve coexisting indicators in known composite words |
| Scanning | Continuous 40 segment shifts / one grid shift per slot, with captured latch/blanking order |
| Initialization | Hold grid CL LOW; explicitly initialize 40-bit registers before enabling grids |
| Replay precedent | Current replay preloads an initial segment word and one-hot grid register under blanking |
| Peripheral handover | Avoid unintended CK rising edges when connecting/disconnecting ESP peripherals |
| Brightness | Reproduce verified blanking timing; dim edge placement needs further extraction |
| Blink | Alternate complete visible/hidden field words without introducing scan gaps |
| Updates | Switch desired frame data coherently; avoid partly updated application frames |
| Stop / failure | Force and retain grid CL LOW; do not leave a selected grid continuously enabled |
| Release | High-impedance is not equivalent to active blanking; define ownership and pull states |
| Validation | Check actual driver-side signals and live output, not merely successful ESP API completion |

GPIO11-14 are reserved peripheral/spare pins in the current replay arrangement
and must remain disconnected from Sony. The 8 MHz ESP DMA sample clock is not
the Sony segment-clock frequency.

## 11. Outstanding work

Fixed-element assignments and matrix orientation are complete at the
reference's functional level; they are not outstanding work.

| Remaining question | How to resolve |
|--------------------|----------------|
| Stable full 26-slot multiplexing | Review driver-side grid alignment, latch/blank sequencing and peripheral transitions; confirm clean live output |
| Generated glyph readability and original Sony font coverage | Review composed glyph designs on the known pixel layout; do not confuse font coverage with missing dot mapping |
| 194 unresolved command bits | Further tracing or focused controlled investigation if needed; no missing reference symbol currently requires them |
| Dim edge placement | Decode CL edges relative to STB from the existing DISPLAY capture |
| Segment CL / both CHG levels | Verify wiring and logic levels at the actual driver pins |
| Power-up flash behavior | Correlate power/control pins with video; zero serial words alone do not explain it |
| Shutdown and stalled-scan safety | Capture original shutdown and validate explicit grid blanking |
| Reliable electrical output | Validate replacement buffers, wiring, supplies and probe paths |

## 12. Evidence references

Earlier five-second captures, decoded counts and tables are catalogued in
[chapter 6](06-direct-display-driving.md#8-snapshot-evidence).
Recent raw/analysis artifacts are retained in the session, not committed:

| Capture prefix | Evidence |
|----------------|----------|
| `sony-original-display-20261108-2000` | Bright/dim timing and indicator hiding |
| `sony-original-memory-1-20261108-2027` | Original MEMORY correlation and red blinking; the bit is now optically confirmed |
| `sony-td-mono-blink` | Earlier reported MEMORY with `0x02` retained in both blink phases |
| `sony-td-primary-b-memory-transition` | Earlier independent MEMORY activation/exit in the B context |
| `sony-td-primary-b-memory`, `sony-td-primary-b-memory-active` | Steady B snapshots; no active `0x02` in their decoded frames |
| `sony-original-restored-poweron` | Startup zero frames, text, bar fill and No DSR |
| `sony-original-mono1-mono2-program1-frequency118-cable-20261108` | Steady setup baseline |
| `sony-original-a16-mono-blink-20261108` | A16 composite and synchronized blink phases |
| `sony-original-mangled-20261108-2038` | Normal decoded D/No DSR/1227/SATELLITE/LNC OFF/full-bar command state |
| `sony-original-red-c-20261108-2120` | Direct red C word `0xC08400`, 634 identical valid frames |
| `sony-stb-input-output-1834` | Clean ESP STB versus extra buffered-output pulses |

Capture prefixes identify `.bin` and analysis artifacts; decoder metadata
must be consulted for channel order. Relevant manufacturer source:
[TD62C950RF datasheet](TD62C950RF.PDF), especially pages 2-4, 6-7 and 9.

## 13. Recovered earlier history and interface distinctions

Review of the earlier conversation recovers additional observations that
should not be lost when consolidating the driver:

| Earlier finding | What is established | Direct-driver implication |
|-----------------|---------------------|---------------------------|
| MEMORY with B blinking | Explicit visual report: fixed MEMORY; slot 25 `0xA61C02` / `0x02`, all other slots unchanged | MEMORY is steady while the red glyph blinks |
| Red C after BLOCK + | User reported C, Frequency 1150 MHz and SATELLITE; captured on the parallel controller bus | C was observed, but no direct TD62 C word was extracted from that capture |
| Double MEMORY press | Temporary "Memory OK" confirmation, returning after about two seconds | Additional observed text state; direct glyph words not yet extracted |
| DIRECT entry | Frequency label alternates with blanks about every 508 ms; numeric field progresses blank, 1, 12, 123, 1234 | Text blink and right-aligned numeric entry can be reproduced by frame composition |
| Frequency plus/minus | Recorded 1161 through 1164 and back to 1161 | More numeric display observations exist on the parallel bus; not additional direct glyph masks |
| SET numeric controls | Red numbers including 1, 3, 4, 5, 6, 13, 14 and 15 were reported and correlated with controller payloads | Wider red-number coverage was observed, but those payload bytes are not TD62 segment words |
| DISPLAY four-state cycle | Normal/auxiliaries, dim/auxiliaries, bright/no auxiliaries, dim/no auxiliaries, restored normal | Combine brightness timing with independent visibility of source/LNC/signal fields |
| Number buttons without visual change | Earlier numbered presses produced short controller reports despite unchanged display packets | No display response alone does not establish a failed key |

The parallel-bus text field matched ASCII with unresolved forced-HIGH capture
bits (`ASCII OR 0xD0` after probe correction). This does **not** constitute a
complete original text-font dictionary and does not encode TD62 pixel words.
Likewise, its red-number byte values cannot be copied into slot 25.
See [bus capture findings](05-bus-capture-findings.md) for that separate interface.

Historical naming must also be retained correctly: the first report called
`0x800` "MONO 2", but the later visual clarification identified it as the
**separate right-hand MONO**. Left MONO indices 1/2 are `0x08/0x04`.
PROGRAM selections `0x80/0x40/0x20` refer to the small digits behind PROGRAM.

The reviewed historical direct captures establish red A, B and D words.
Red C was first recovered as an earlier observation on the parallel interface,
and has subsequently been captured directly as documented below.
Additional red alphabet shapes remain derived targets, subject to the physical
segmented geometry.

## 14. Red C prediction and direct validation

Before a passive five-second capture of user-reported lit C, the speculative
letter-only prediction was `0xA41800`: D with assumed right-side bit `0x0400`
removed. The capture instead produced **slot 25 `0xC08400`** in all 634
complete valid frames. The prediction was rejected; its assumed physical
segment assignment must not be used for further extrapolation.

Other captured fields: No DSR, SATELLITE/LNC OFF (slot 23 `0x0E`),
full signal indicator (slot 0 `0x7FFF`), slot 24 zero. All decoded normal
intervals had 40 segment clocks and one grid clock.
STB HIGH widths included approximately 21.375-21.5 and 25.125-25.25 us
as well as the previously documented 2.75-2.875 us. Thus the short STB
window is not universal across all original-controller states.

Evidence: `sony-original-red-c-20261108-2120.bin` and
`sony-original-red-c-20261108-2120-analysis.json`. This is a captured C
pattern. It did not independently establish segment geometry at that time;
the later one-hot inventory did, as listed in section 6.

## Native VFD text API and scrolling

The local firmware exposes a text-display interface, independent of the browser:

```c
sony_vfd_show("Now Playing: blabla", true, true);
/* UTF-8 text, scroll enabled, dim enabled */
```

The driver owns UTF-8 lookup, glyph selection, the nine-character message
window and scrolling. Sony/custom glyphs remain authoritative. Only slots
22 down to 14 change; frequency, source, indicators and red fields are preserved.
The application must first start an explicitly authorized continuous Display
scan. Text calls never start output and return an invalid-state error if the
display is stopped or another scan mode is active.

| Setting | Behavior |
|---------|----------|
| At most nine display characters | Static text, padded with blanks |
| Longer text with scroll enabled | One-second initial pause, then one-cell steps every 300 ms |
| Loop separator | Four blank cells; pause again at the first window |
| Scroll disabled | First nine display characters, without motion |
| Dim enabled | Measured delayed grid CLEAR enable |
| Dim disabled | Original bright fixture CLEAR waveform |
| New title | Atomically replaces the old title and restarts its initial pause |
| Identical rendered title / scroll setting | Retains the current scroll position |
| Empty title | Clears only the message field |

POST `/api/screen` accepts the same operation:

```json
{"sample":"Text","text":"Now Playing: blabla","scroll":true,"dim":true,"live_confirmed":true}
```

An isolated bench uses `bench_confirmed:true` instead. Scroll and dim default
to true; supplied values must be JSON booleans. Raw `sample:"Display"` updates
cancel scrolling. Stop/blank retain their existing behavior.

Native UTF-8 validation rejects malformed input without replacing the current
title. Limits are 1,024 UTF-8 bytes, 256 display clusters and the existing
2,047-byte JSON request body. Native conversion supports the catalogue's
composed/decomposed accents, combining marks, presentation selectors, emoji
modifiers, ZWJ grouping and regional-indicator pairs. Other complex-script
shaping, bidi layout and complete Unicode grapheme segmentation are not
implemented; unmatched characters retain the `*` fallback.

ICY metadata can submit titles through this API when the screen feature is
compiled, but never enables output itself. Non-UTF-8 metadata requires prior
charset conversion; it is not automatically guessed. The current screen-only
profile does not enable radio playback.

The native font is generated from the same Sony/custom dictionaries as the
bitmap catalogue. This implementation is local, not yet deployed or validated
on the Sony. Timing is quantized by the existing roughly 63 ms DMA chunks;
the backend is not yet a persistent gap-free display scan.
