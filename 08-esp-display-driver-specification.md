# ESP32 direct display-driver specification

Consolidated evidence through 2026-11-08. Detailed capture history and
datasheet comparisons: [Direct Display driving](06-direct-display-driving.md).

## 1. Scope and evidence status

This specifies direct control of the IC702 grid driver and IC703 segment
driver, both Toshiba TD62C950RF. It is **not** the parallel command protocol
between the original controllers.

The display has no measured high-level "show MEMORY" command. The ESP must
continuously scan 26 slots, sending a 40-bit segment word for each slot.
For example, the MEMORY candidate is **slot 25 mask `0x02`**, not a standalone
bus command. The same mask in another slot has a different meaning.

| Status | Meaning |
|--------|---------|
| Confirmed | Datasheet-defined behavior or isolated, repeatable capture correlation |
| Captured | A complete observed pattern; individual physical elements may not be isolated |
| Candidate | Strong correlation, but independent confirmation is missing |
| Unknown | Not mapped; do not assign a meaning or actively test without precautions |

Distinguish **field control** from **individual physical-bit verification**.
The PROGRAM and MONO groups, full signal indicator, source/LNC selections
and programmable text layout are usable even where a label's individual bit
has not been isolated. A missing original Sony glyph does not prevent generating
that character from our own 5x7 font.

### Capability inventory

| Screen element | What we have | What is still unresolved |
|----------------|--------------|--------------------------|
| Text letters and digits | Addressable 5x7 layout, message/frequency slot order and captured glyph examples; arbitrary letters/digits can be generated | Complete original Sony font is not catalogued; generated orientation needs live verification |
| PROGRAM label with small 1/2/3 | Complete visible/hidden words and isolated small-digit selection masks | Separate label bit is not independently isolated |
| Left MONO label with small 1/2 | Complete words and isolated index masks | Label `0x10` is a candidate individual assignment |
| Separate right-hand MONO | Independently isolated `0x800` | No additional mapping needed for the captured indicator |
| MEMORY | Repeated independent activation/exit correlation for slot 25 `0x02` | Physical drive verification |
| Red A/B/C/D | Direct words for all four letters are now captured, including C `0xC08400` | General red segment geometry remains incomplete |
| Other red letters | Can potentially be composed from the red segment geometry | Requested E/F/G/H/I/L/K/M/N/O/P/Q/R/S/T/U/V/W/X/Y/Z are derived targets, not verified captured words |
| Red numbers | Captured composite A2, A9, A16 and other reported numeric states | General digit/segment mapping is incomplete |
| Signal indicator full | Captured slot 0 `0x7FFF`, with startup transition | Individual bar order and intermediate levels |
| Lower dotted signal scale | User-observed at startup and in older D/No DSR captures associated with `0x4000` | Whether dots share an enable, and exact individual physical-bit assignments |
| LNC OFF/A/B | Complete composite states and isolated selection masks | Separate LNC-label bit verification |
| CABLE/SATELLITE | Independently isolated source masks | No additional mapping needed for the captured selections |
| Bright/dim, hide, blink | Captured electrical timing and data sequences | Dim edge placement and live generated-output verification |

Firmware 0.3.7 can replay captured waveforms. Readable live Frequency output
has been observed, but reliable live operation remains unresolved. Simultaneous
STB measurements showed a clean ESP input to the buffer and extra short pulses
at its output. Buffer, wiring and analyser/probe causes are not fully separated.
An arbitrary-screen driver is not yet implemented or electrically validated.

## 2. Electrical interface and signal assignments

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

Datasheet minima at its stated test conditions: CK/STB pulses 70 ns,
DATA setup/hold 20 ns, CK rising to STB falling 50 ns. An 8 MHz analyser
has 125 ns resolution and cannot certify analogue margins or threshold crossings.

## 5. Screen slot map

| Slot | Intended field | Status / limits |
|------|----------------|-----------------|
| 0 | Signal-strength indicator and lower dotted scale | Captured off, reported dots/startup and full words; individual elements unknown |
| 1-2 | Unassigned | Zero in documented captures |
| 3-5 | MHz suffix | Captured; slots read `z`, `H`, `M` |
| 6-10 | Frequency | Captured right-to-left digits and decimal point |
| 11-13 | Unassigned | Zero in documented captures |
| 14-22 | Nine-character message | Captured right-to-left text |
| 23 | CABLE/SATELLITE and LNC | Isolated masks plus label candidate |
| 24 | PROGRAM label and small 1/2/3, left MONO label and small 1/2, separate right MONO | Complete groups and isolated selections; individual label bits partly unresolved |
| 25 | Red letters/digits and other indicators | Composite patterns; MEMORY candidate |

Do not assume the unassigned slots or unused word bits are safe general-purpose
pixels. Physical connectivity has not been fully traced.

## 6. Indicator masks and their outcomes

Masks below are hexadecimal within the named 40-bit slot word.
Set/clear operations must preserve the other fields in the same slot.

### Slot 0: signal strength

| Word / mask | Captured outcome | Status |
|-------------|------------------|--------|
| `0x00` | No signal indicator commanded in the Frequency 118.0 setup baseline | Captured |
| `0x4000` | Reported dotted signal scale in older D/No DSR states; initial Frequency startup state before full bar | Captured/user-correlated; exact physical elements unknown |
| `0x7FFF` | Reported full signal bars | Captured |
| `0x3FFF` | Bits added when startup changes to full scale about 0.986 s later | Confirmed data delta; individual dot/bar positions unknown |
| Individual bits 0-13 | Which dot/bar, direction and level progression | Unknown |
| Bit 14 (`0x4000`) | Label, baseline dot or other element | Unknown |

The startup observation alone does not establish a 15-step meter or one
physical bar per bit. A synchronized visual comparison or safe bit walk is needed.

### Slot 23: source and LNC

| Mask | Visible outcome | Status |
|------|-----------------|--------|
| `0x01` | CABLE | Confirmed |
| `0x02` | SATELLITE | Confirmed |
| `0x04` | LNC label | Candidate |
| `0x08` | LNC OFF | Confirmed |
| `0x10` | LNC A | Confirmed |
| `0x20` | LNC B | Confirmed |
| Other bits | Unassigned | Unknown |

Captured composite words: CABLE `0x01`; CABLE/LNC OFF `0x0D`;
SATELLITE/LNC OFF `0x0E`; SATELLITE/LNC A `0x16`;
SATELLITE/LNC B `0x26`.

### Slot 24: PROGRAM and MONO

| Mask | Visible outcome | Status |
|------|-----------------|--------|
| `0x80` | PROGRAM 1 | Confirmed |
| `0x40` | PROGRAM 2 | Confirmed |
| `0x20` | PROGRAM 3 | Confirmed |
| `0x08` | Left MONO index 1 | Confirmed |
| `0x04` | Left MONO index 2 | Confirmed |
| `0x10` | Left MONO label | Candidate |
| `0x800` | Separate right-hand MONO indicator | Confirmed; not the left index 2 |
| `0x100` | Element retained with PROGRAM selections during setup blink; possible PROGRAM label | Candidate interpretation, not isolated; do not label MEMORY |
| Other bits | Unassigned | Unknown |

| Captured state | Visible word | Hidden word |
|----------------|--------------|-------------|
| PROGRAM 1, left MONO 1, right MONO | `0x998` | `0x180` |
| PROGRAM 2, same MONO state | `0x958` | `0x140` |
| PROGRAM 3, same MONO state | `0x938` | `0x120` |
| PROGRAM 3, left MONO 1 only | `0x138` | `0x120` |
| PROGRAM 3, left MONO 2 only | `0x134` | `0x120` |
| Primary No DSR | `0x00` | Steady |

The same `0x998` word was also captured steady in the Frequency 118.0
baseline. Blinking is a sequence of words, not a property of that word alone.
The `0x80/0x40/0x20` selections correspond to the **small 1/2/3 behind
PROGRAM**, not three separately established PROGRAM labels. Complete captured
group words let us reproduce the label and digit together without first
isolating the label bit.

### Slot 25: MEMORY and red display

| Mask / field | Outcome | Status |
|--------------|---------|--------|
| `0x02` | Steady MEMORY indicator | Confirmed by earlier explicit visual report and isolated B-context transition; physical bit-drive test pending |
| Red letter bits | Arbitrary A/B/D and other letters | Complete examples captured; individual segment map unknown |
| Red numeric bits | Arbitrary one/two-digit values | Complete examples captured; digit separation incomplete |
| Other indicator bits | Unassigned | Unknown |

During the isolated MEMORY sequence, slot 25 alternated `0x478C02` /
`0x02`, then returned to `0x478C00`. The `0x02` bit remained HIGH in
both red-character blink phases. It does not prove MEMORY itself blinked.
No other slot changed during that first sequence.
The earlier user report explicitly described MEMORY as a fixed element with
only B blinking. Thus the observed behavior is **steady MEMORY, blinking red
character**, rather than an unknown MEMORY blink state.

This is also supported by earlier scans, not only the latest activation:

| Earlier capture | Valid frames / slot-25 evidence | Interpretation |
|-----------------|--------------------------------|----------------|
| `sony-td-mono-blink` | 634 frames; `0x800022` / `0x02` | MEMORY was reported; bit `0x02` persists in both phases |
| `sony-td-primary-b-memory-transition` | 3,802 frames; `0xA61C00`, `0xA61C02`, `0x02` | B-context activation adds `0x02`; exit restores `0xA61C00` |
| `sony-td-primary-b-memory` | 633 frames; steady `0xA61C00` | No active MEMORY bit in the sampled frames despite the filename |
| `sony-td-primary-b-memory-active` | 634 frames; steady `0xA61C00` | No active MEMORY bit in the sampled frames despite the filename |

In the earlier transition capture, the first activation appears at 12.560 s
and exits at 17.664 s; another activation starts at 27.286 s. Slot 24 remains
zero throughout its decoded frames. The repeatable bit addition across red
A and B contexts establishes a stronger MEMORY mapping than a single snapshot.
The two steady captures must not be treated as positive evidence merely because
their filenames contain "memory".

| Reported red field / context | Complete slot-25 word |
|------------------------------|-----------------------|
| Dashes `--`, setup visible | `0x800022` |
| A, setup visible | `0x478C22` |
| A2, setup visible | `0x478D6E` |
| A9, setup visible | `0x478DBE` |
| A16, setup visible | `0x478FF6` |
| D9, setup visible | `0xA41DBE` |
| Setup hidden phase | `0x02` |
| A alone, MEMORY inactive baseline | `0x478C00` |
| D alone, primary No DSR | `0xA41C00` |
| B alone, primary No DSR | `0xA61C00` |
| C alone, primary No DSR (2026-11-08) | `0xC08400` |
| User reported two `1` indications, Frequency 118.0 setup | `0x18`; individual assignments unknown |

These words include coexisting indicators. They are not isolated glyph masks.
Measured XOR deltas: A to A2 `0x14C`; A2 to A9 `0xD0`;
A9 to A16 `0x248`; primary D to B `0x20000`.

## 7. Text and frequency encoding

Observed text occupies the low 35 bits of each 40-bit word. To render a
captured glyph, reverse those 35 bits and split into seven rows of five pixels:

```python
pixels = f"{word:040b}"[5:][::-1]
rows = [pixels[i:i + 5] for i in range(0, 35, 5)]
```

This is an observed rendering convention. A generated font needs live
orientation verification. Do not apply this convention to red or indicator slots.
Message slot order is right-to-left: slots 14 through 22 are the reverse
of the nine-character screen reading order.

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
font**. Its remaining glyph words, punctuation, reserved upper five bits and
physical orientation of arbitrary generated glyphs are not fully validated.

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
beyond ASCII. The following are required extension categories, **not a
claim of currently implemented glyphs**:

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
musical-note character. These requirements do not change the current editor.

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
| Mapping | Keep slot-specific masks separate; unknown fields remain explicitly unassigned |
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

| Missing information | How to resolve |
|---------------------|----------------|
| Individual bar/dot positions | Correlate intermediate levels with images or perform a safe bit walk |
| MEMORY physical verification | Capture correlation is established; verify the visible element with video or controlled bit drive |
| Slot 24 `0x100`, possible PROGRAM label | Isolated visual transition or safe bit walk |
| MONO/LNC label candidates | Independently switch or map each label |
| Red glyph segments and digit separation | Use captured A/B/C/D words; derive additional letters only after establishing segment geometry |
| Full generated text/font orientation | Render known asymmetric glyphs on the validated interface |
| Unassigned slots/bits | Schematic tracing and controlled mapping; no guessed assignments |
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
| `sony-original-memory-1-20261108-2027` | MEMORY candidate and red blinking |
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
pattern, not proof of an independently mapped physical segment geometry.
