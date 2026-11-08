# Direct Display driving

Bench findings: 2026-11-04; datasheet/replay comparison: 2026-11-08.

For consolidated driver requirements, slot masks, captured outcomes and explicit
unknowns, see the [ESP32 display-driver specification](08-esp-display-driver-specification.md).

**Status: passively decoded; experimental replay is not working correctly
on the live display.** Captures at the inputs
of the two TD62C950RF display drivers reveal a repeating 26-slot scan, readable
character matrices, red-display patterns and several isolated annunciator bits.
This is the measured basis for Option 2: replacing IC701's display output with
an ESP32. The earlier replay had extra startup/shutdown clock edges and
showed malformed characters on Sony. Version 0.3.5 fixes those boundaries
on the analyser; its live display result is not yet verified.

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

The original chapter/user tracing lists DATA 29, CLOCK 34, STB 26 and grid
CLEAR 24. The uploaded [Toshiba TD62C950RF/LF datasheet](TD62C950RF.PDF),
page 2, uses the following **manufacturer top-view pin numbers**:

| Signal | Manufacturer pin | Earlier reversed-count reference (`61 - pin`) |
|--------|------------------|----------------------------------------------|
| S-in / DATA | 32 | 29 |
| CK / CLOCK | 27 | 34 |
| STB | 35 | 26 |
| CL / CLEAR | 37 | 24 |
| CHG | 25 | 36 |

This exact reversal reconciles the earlier references with the datasheet;
it is not a second manufacturer pinout. RF/LF package drawings are mirrored,
but their signal-to-number assignments are the same. Locate pin 1 from the
package marking and verify continuity before using either counting convention.
In manufacturer numbering, pins 24, 26 and 34 are NC, and pin 29 is S-out:
do not attach signals using the earlier numbers as literal manufacturer pins.
The user's traced CLEAR signal is on the grid driver only; the segment-driver
CL and both CHG connections have not been independently verified here.

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
physical grid wiring or initial shift-register contents.

### Datasheet-confirmed behavior

Pages 2-4 and 9 establish:

- **CK rising shifts DATA into the 40-bit shift register.**
- **STB HIGH is transparent; STB LOW holds the output latch.** The falling
  transition closes the latch, not the rising transition. STB rising remains
  a useful *decoder boundary*, but must not be described as an edge-triggered
  latch operation.
- **CL LOW overrides all OUTn LOW**, regardless of STB/CHG. It does **not**
  reset the shift register: S-out continues to shift while CL is LOW.
- The page-3 truth table and page-4 chart show **CHG HIGH, with CL HIGH,
  overrides all OUTn HIGH**. CHG has an internal pull-down; CL has an internal
  pull-up. The CHG prose on page 3 appears inconsistent with its truth table;
  use the block diagram/truth table/chart, not that sentence alone.
- After 40 transmitted bits, OUT1 corresponds to the last transmitted bit
  and OUT40 to the first, when the latch is transparent/updated and overrides
  are inactive. RF/LF changes physical shift direction, not this logical
  sequence. Board grid/segment wiring still determines visible placement.

Consequently CL LOW on the actual grid CL input provides an output-LOW
override, rather than register initialization. Blank outputs and initialized
registers are separate requirements. A verified grid CL LOW connection can
prevent positive grid drive while initializing; floating ESP/buffer inputs
are not equivalent to holding this input LOW.

### Match to the recorded Sony slot

Taking an ordinary STB rising transition as time zero:

| Event | Original Frequency capture | Meaning with the datasheet |
|-------|----------------------------|----------------------------|
| CL is already LOW | Before time zero | Grid outputs forced LOW during update |
| STB rises | 0 us | Both latches become transparent |
| STB falls | 2.75-2.875 us later | Hold the newly exposed register values |
| Grid CK falls, then rises | About 4.75 / 6.625 us | Shift the next grid selection while latch is held |
| CL rises | About 10.375-10.5 us | Release grid output override |
| Segment CK traffic begins | About 20.875-21 us | Shift next segment word while latch is held |
| CL falls before next STB rise | About 12.375-12.5 us total LOW interval | Blank grid outputs for next update |

Thus the one grid shift and 40 segment shifts in a slot prepare values for
the following STB HIGH window. The display is not following every serial
bit during the long STB LOW interval. CL overlaps the latch-update window
and the following grid shift. The captured LOW interval is output blanking,
not evidence that a shift-register reset occurred.

### Timing limits versus analyser measurements

Page 7 specifies the following limits at its stated 25 C, VDD=5 V,
HVCC=70 V and test-load conditions; page 9 defines the measurement edges.
Compare them with the original Frequency fixture and the buffered ESP repeat:

| Requirement | Datasheet minimum | Original measured minimum | Buffered replay measured minimum |
|-------------|-------------------|---------------------------|----------------------------------|
| CK pulse width | 70 ns | Segment LOW/HIGH: 375/375 ns | Segment LOW/HIGH: 250/375 ns |
| DATA setup before CK rising | 20 ns | Segment: 375 ns | Segment: 250 ns |
| DATA hold after CK rising | 20 ns | Segment: 375 ns | Segment: 375 ns |
| STB pulse width | 70 ns | HIGH: 2.75 us | HIGH: 2.75 us |
| CK rising to STB falling setup | 50 ns | Segment: 52.875 us | Segment: 52.875 us |

Grid CK LOW is at least 1.875 us in the original and 1.75 us in the buffered
repeat; grid DATA setup/hold minima are 80/210.5 us in both. These measurements
exclude ESP peripheral startup/shutdown transitions. They show substantial
sampled margins, but **8 MHz acquisition has 125 ns resolution** and cannot
certify 20 ns margins, analogue edge quality or exact threshold crossings.
Page 6 requires VIH >= 0.7 VDD and VIL <= 0.3 VDD: at VDD=5 V these are
3.5 V and 1.5 V, respectively. A direct 3.3 V HIGH is not guaranteed; the
5 V AHCT interface remains necessary for the current arrangement.

The 8 MHz replay sample clock is on an unconnected peripheral WR pin;
it is not an 8 MHz CK input to Sony. The reproduced segment CK pulses are
much slower and separated by the captured gaps.

### ESP replay boundary defects

The two buffered Frequency captures on 2026-11-08 use a different analyser
order from the originals: CH1-CH6 = GPIO39/40/41/42/47/48. Remap to the
original bit order before comparing.

All 3,326 comparable complete latch intervals match the original segment
words and grid markers, with 40 segment clocks and one grid clock each.
However, the first interval has **41 segment clocks and two grid clocks**.
Peripheral takeover adds a rising edge before the first recorded clock.
After the transfer, both clocks fall LOW and later rise during GPIO
restoration. These are genuine shifts according to the datasheet, not
harmless GPIO state changes. The extra grid shift is a plausible alignment
fault, not yet a proven explanation of the live half-characters.

Firmware 0.3.5 addresses these defects with 40 deliberate initialization
shifts under CL LOW, preloading the captured initial segment word and grid
OUT1. An inverted memory/GPIO clock path makes peripheral idle LOW produce
physical CK HIGH. The actual clock pins are not attached during bus creation
and are detached to HIGH GPIO registers before peripheral deletion.
Replay finishes by explicitly retaining CL LOW and STB LOW, not by relying
on the arbitrary final captured state.

Three new buffered captures verify initialization and preload, normal first
scan slot (40/1 clocks), all comparable segment words and grid markers
(3,326 intervals in each Frequency replay; 3,378 in blink), and no additional
shutdown CK edges. Initial GPIO HIGH establishment may create one edge from
the pre-boot/released state; the following 40 deliberate initialization
shifts replace its effect while CL stays LOW. The initialization period
must not be counted as a normal scan slot. Correct live display operation
with this version remains unverified.

### Blink behavior

The setup/program snapshots contain two frame patterns. Each blink phase lasts
about **508.716 ms**, giving a full cycle of about **1.017 seconds**.
The MONO and red fields change between phases while PROGRAM selection remains
present. The later primary No DSR snapshots are steady, with one frame pattern.

### DISPLAY brightness and indicator hiding

A passive 20-second, 8 MHz capture on 2026-11-08 recorded four DISPLAY
presses with the original Sony wiring restored. Analyser CH1 through CH6
were FLGDATA, FLGCLK, FLCLEAR, FLSTB, FLSDATA and FLSCLK.
The raw capture and decoded results are session artifacts:
`sony-original-display-20261108-2000.bin` and
`sony-original-display-20261108-2000-analysis.json`.

Two brightness timing modes are evident:

| Mode | Typical grid CLEAR LOW duration | Measured CLEAR HIGH fraction |
|------|---------------------------------|-----------------------------|
| Bright | 12.375-12.5 us per slot | 95.89% |
| Dim | approximately 202.875 us per slot | 32.59% |

Since grid CL LOW forces grid outputs LOW, the longer blanking interval
reduces grid-on time. The HIGH fractions are electrical enable duty, not
measurements of perceived brightness. STB HIGH remains 2.75-2.875 us in
both modes, and every decoded normal interval retains 40 segment clocks
and one grid clock.

The capture starts dim with indicators present. Near 2.47 seconds it changes
to bright while slot 0 (signal bars, `0000007FFF`) and slot 23
(CABLE/LNC OFF, `000000000D`) become zero. Near 5.1 seconds it becomes dim
without changing segment words. Near 7.36 seconds it returns to bright
and restores those indicator words; their restoration spans two frames.
No other segment-word changes were observed. Thus this capture distinguishes
brightness control through CLEAR timing from indicator hiding through data.

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

Rising-edge sampling produces recognizable glyphs and is now confirmed by
the datasheet. Falling-edge decoding was also examined and differs for some
words; it is not the specified shift edge. Analogue setup/hold margins at
the chip and serial-bit-to-visible-segment mapping still need verification.

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
| 0 | Signal-strength indicator candidate: `0000004000` at startup, then `0000007FFF` at reported full scale; individual segments unresolved |
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
transition implicates its bit in slot 24. Do not label `0x100` as MEMORY.

## 7. Red display: slot 25

These are complete measured words, including any coexisting indicators.
Individual segment masks are not yet fully assigned.

| User-reported red display/context | Slot-25 word |
|----------------------------------|--------------|
| Dashes `--`, setup visible phase | `0000800022` |
| A, setup visible phase | `0000478C22` |
| A2, setup visible phase | `0000478D6E` |
| A9, setup visible phase | `0000478DBE` |
| A16, PROGRAM 1/MONO setup visible phase (2026-11-08) | `0000478FF6` |
| D9, after source change, setup visible phase | `0000A41DBE` |
| Hidden phase of the above setup states | `0000000002` |
| D alone, primary No DSR, steady | `0000A41C00` |
| B alone, primary No DSR, steady | `0000A61C00` |
| C alone, primary No DSR, steady (2026-11-08) | `0000C08400` |

A -> A2 has XOR `0x14C`; A2 -> A9 has XOR `0xD0`.
The user-reported lit C was captured in
`sony-original-red-c-20261108-2120`: 634 identical valid frames with
slot 25 `0000C08400`. A pre-capture guess of `0xA41800` was rejected.
The source-change snapshot was later explicitly confirmed as **D9**, not A9:
the digit 9 remains while the letter changes. D -> B in the primary snapshots
has XOR `0x20000`. These deltas are not complete encodings for arbitrary
letters/digits, and the setup and primary words should not be mixed without
accounting for their other bits.

### MEMORY mapping

In a 30-second passive capture on 2026-11-08, the user pressed MEMORY and
reported the MEMORY indicator appeared or blinked; pressing 1 produced no
visible change. Around 4.020 seconds slot 25 changed from `0000478C00` to
`0000000002`, then alternated between `0000478C02` and `0000000002` at
roughly 509 ms intervals. At 9.107 seconds it returned to `0000478C00`.
No other slot changed during this sequence. Slot 25 **bit `0x02` is therefore
a strong MEMORY indicator candidate**, but it remained set through both
captured red-character blink phases; the report does not establish that the
MEMORY indicator itself blinked. Later red-word changes in this capture lack
independently timestamped button annotations and are not mapped here.

Earlier `sony-td-primary-b-memory-transition` decoding independently shows
slot 25 `0000A61C00` changing to alternating `0000A61C02` /
`0000000002` at 12.560 s and returning to `0000A61C00` at 17.664 s.
Another activation starts at 27.286 s. All 3,802 valid frames retain slot 24
zero. The earlier `sony-td-mono-blink` also contains `0x02` in both phases
with MEMORY reported. Together these establish a repeatable MEMORY capture
correlation for slot 25 `0x02`, not slot 24 `0x100`.
The earlier visual report explicitly identified MEMORY as steady while B
blinked, consistent with `0x02` remaining set in both phases.

The separately named `sony-td-primary-b-memory` and
`sony-td-primary-b-memory-active` captures contain 633 and 634 valid frames
respectively, all with slot 25 `0000A61C00`; they do not capture an active
MEMORY bit. Physical element verification remains pending.

Evidence: the earlier decoded artifacts above and session artifacts
`sony-original-memory-1-20261108-2027.bin` /
`sony-original-memory-1-20261108-2027-analysis.json`.

### Frequency 118.0 baseline

On 2026-11-08 the user reported "mono 1 mono 2 program 1 1 Frequency
118.0Mhz Cable" lit. A passive five-second capture had 634 complete valid
frames, all identical: slot 0 `0000000000`, slot 23 `0000000001`,
slot 24 `0000000998` and slot 25 `0000000018`. The Frequency text and
118.0 MHz words matched the reported text. No blink phase was observed
during the capture. The two reported `1` indications are not independently
assigned to slot-25 bits by this single snapshot.

Evidence: session artifacts
`sony-original-mono1-mono2-program1-frequency118-cable-20261108.bin` and
`sony-original-mono1-mono2-program1-frequency118-cable-20261108-analysis.json`.

### A16 blinking baseline

A subsequent five-second capture with user-reported red A16, PROGRAM 1,
MONO 1 and blinking MONO 2, Frequency 118.0 MHz and CABLE contained
634 complete valid frames. Slot 24 alternated `0000000998` /
`0000000180`, and slot 25 alternated `0000478FF6` / `0000000002`.
Both words changed in the same decoded frames, with phases approximately
509 ms long. All other slot words matched the preceding Frequency 118.0
baseline. This measures an A16 composite word, not separate encodings
for the letter, tens digit and units digit.

Evidence: session artifacts `sony-original-a16-mono-blink-20261108.bin`
and `sony-original-a16-mono-blink-20261108-analysis.json`.

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
The user reported antenna-strength dots/bars. See the startup comparison
below; individual bar transitions and physical bit positions remain unverified.

### Startup signal-strength comparison

The restored-original power-on capture was decoded across its full 30 seconds
using CH1-CH6 = FLGDATA, FLGCLK, FLCLEAR, FLSTB, FLSDATA, FLSCLK.
Times are relative to the beginning of the recording, not power-on:

| Time | Decoder slot 0 | Context |
|------|----------------|---------|
| 7.810 s | `0000000000` | First structurally valid all-zero frame |
| 8.670 s | `0000004000` | Frequency, 50.0 MHz and other text appear |
| 9.657 s | `0000007FFF` | Only slot 0 changes; reported full signal bar |
| 10.729 s | `0000007FFF` | Message changes to No DSR; slot 0 remains unchanged |

The slot-0 transition occurs about 0.986 s after the Frequency frame and
adds bits `0x3FFF`; bit `0x4000` was already set. The user reports that the
indicator's lower dots are visible at startup before the bar becomes full.
This strongly supports slot 0 as the indicator's control word, but the
capture does not independently identify the physical segment controlled by
each bit, whether `0x4000` is a label/dot or another element, or whether the
full word contains non-bar elements. The all-zero decoded frame must not be
assumed to prove that the whole VFD was visibly dark during power-up, since
the other driver control pins and startup power conditions were not measured.
Correlate intermediate levels with visual observations or use a safe
individual-bit walk after the electrical interface is validated.

Evidence: session artifacts `sony-original-restored-poweron.bin` and
`sony-original-restored-poweron-analysis.json`.

## 9. What remains before direct driving

The measured model suggests preparing 26 segment words, shifting 40 segment
bits and one grid bit per slot, and reproducing the measured latch/blanking
sequence. The datasheet establishes the input semantics and version 0.3.5
passes buffered boundary checks, but its live display result is unverified.

Before any active experiment:

1. Verify actual manufacturer-numbered signal connections, CHG/CL levels,
   analogue timing and power-up initialization. CK rising, STB transparent
   HIGH/held LOW and CL output override LOW are now datasheet-established.
2. Isolate IC701's outputs before connecting another push-pull driver; never
   actively oppose the original controller.
3. Provide a verified electrical interface. ESP32 GPIO is not 5 V tolerant;
   do not assume 3.3 V meets the Sony drivers' HIGH threshold.
4. Establish safe blanking and grid sequencing so a stalled scan cannot leave
   a grid continuously energized. Preserve the existing VFD supply/filament
   arrangements and keep probes off high-voltage outputs.
5. Confirm the MEMORY candidate and isolate the left MONO label, LNC label, antenna bars and remaining
   indicators; expand red character and text font coverage.
6. Extend the measured two-mode brightness behavior to other operating
   states and complete startup/shutdown mapping for a full replacement.

The current result is a reproducible passive display map with specific
confirmed transitions, not a finished ESP32 driver.
