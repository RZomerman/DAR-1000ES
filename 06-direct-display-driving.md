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

This rendering convention was subsequently verified optically for slots
1-22 in the complete camera inventory below. It is not a complete font.
Do not apply it to the
annunciator/red words as though those were ordinary 5x7 characters.

| Slot(s) | Observed use |
|---------|--------------|
| 0 | Signal strength: 14 cumulative bars in bits 0-13 (`0x3FFF`), bottom dots at `0x4000`; visually verified below |
| 1-2 | Camera-verified 5x7 matrices in bits 0-34; bits 35-39 showed no resolved response |
| 3-5 | `z`, `H`, `M`: reverse scan order for MHz |
| 6-10 | Frequency, right to left; decimal point occupies a slot |
| 11-13 | Camera-verified 5x7 matrices in bits 0-34; bits 35-39 showed no resolved response |
| 14-22 | Nine-character message, right to left; camera-verified 5x7 matrices in bits 0-34 |
| 23 | Source/LNC annunciators in bits 0-5; lower numbered markers 1-16 in bits 6-21 |
| 24 | SPEECH/MUSIC/MONO, PROGRAM, TUNED and tuning triangles in bits 0-14 |
| 25 | STEREO/MEMORY, seven-segment digit, adjacent "1" and 14-element red glyph in bits 0-23 |

On 2026-11-12, a held-mode slot-11 sweep of words `0000000001`,
`0000000002`, `0000000004` and `0000000008`, approximately two seconds each
at 20 us CLEAR HIGH / 980 us LOW (2% duty), produced the user report
"right top.. dots only". All other slots were zero. This locates the
observed elements but does not assign individual dot positions to these bits.

In a separate held-mode slot-0 probe on 2026-11-12, words `0000000001`,
`0000000010` and `0000000100` illuminated progressively small, medium and
large signal-strength elements, with intermediate elements skipped.
Word `0000004000` illuminated bottom dots. Each word ran for approximately
two seconds at 2% CLEAR duty with all other slots zero. This visually locates
signal-strength elements in slot 0; exact sizes, positions and intermediate
bit assignments remain to be mapped.

The user then explicitly confirmed the cumulative slot-0 progression
`0x01`, `0x03`, `0x07`, `0x0F`, ... through `0x3FFF`: one bar, two bars,
three bars, and so on through 14 bars. Each step ran for approximately one
second at the same 2% CLEAR duty, with `0x4000` kept clear. Thus the bar
mask for `n` bars is `(1U << n) - 1`, for `n = 0..14`; add `0x4000` only
when the bottom dots are also wanted. Bars plus dots give the captured
full-scale word `0x7FFF`. This is a visually verified mapping, not an
electrical waveform signoff.

An OBSBOT Tiny 4K webcam independently checked this progression on
2026-11-12. For each requested level 0..14, five local camera frames were
captured after the firmware reported submission of the corresponding word.
Bright-component counts in the signal-meter crop matched every requested
level in all 75 frames; a contact sheet was also visually inspected.
Five further frames after Stop contained zero illuminated bars. The test
used the same held slot 0, 20 us HIGH / 980 us LOW (2% duty), with bottom dots
off. Camera images verify visible bar counts in that crop, not electrical
timing, the absence of faint ghosting, or the rest of the display.
The local session artifacts are under
`webcam-bars/progression-20261112-231552/` (`report.json`, raw meter crops
and `contact-sheet.png`).

A grouped live probe tested slots 1, 2, 12 and 13 in that order,
each with `0000000001` then `0000000100`, approximately two seconds per word
at the same held-mode 2% duty. The user reported dots on the right, apparently
in the matrix. This report does not identify each slot/bit separately, and
the bottom 1-16 markers remain unmapped. Other bits in these slots were not
excluded as possible marker controls.

### Camera sweep of slot 1

On 2026-11-12, a complete held-mode slot-1 sweep tested all 40 one-hot
words, from `0000000001` through `8000000000`, at 20 us CLEAR HIGH /
980 us LOW (2% duty). Five camera frames were captured per word. After
correcting camera framing, bits 0-34 each produced one resolved cyan
matrix dot in all five frames; bits 35-39 produced no resolved response
in the visible crop. The dots occupy a compact apparent 5x7 character
area near the right of the display. This is not a mapping of the lower
1-16 markers.

For this earlier run, precise bit-to-pixel coordinates remain provisional:
the view moved during the sweep, and translation-only registration failed its
grid-fit check. Do not treat the fitted coordinates as a verified mapping.
No visible response is not proof that a bit is unused.
Stop was confirmed by the API, with zero resolved optical components in
the post-Stop images. The usable local session evidence is under
`webcam-bars/slot-1-20261112-232255/` (`report.json`, raw display crops and
`bit-atlas.png`). The earlier `slot-1-20261112-231955` run missed the display
because of changed framing; its optical results were rejected and its
unusable images discarded. Neither run provides electrical verification.

### Partial full-display camera inventory

On 2026-11-13, a shaded-camera inventory verified all 40 one-hot words in
slots 0-13: **560 commands**, with 470 resolved optical responses and 90
without a resolved response. Five frames contributed to each consensus
mask. The display remained at 20 us CLEAR HIGH / 980 us LOW (2% duty);
only camera exposure/gain changed. Calibration resolved blank, one/seven/
14 bars, a red segment and a matrix dot. Every accepted block had
before/after 14-bar position checks, confirmed API Stop and a blank
post-Stop optical image.

Slots 1-13 each resolved one dot for every bit 0-34. In the camera view,
bit 0 is top-left, bit 4 top-right, bit 5 starts the next row and bit 34
is bottom-right. For zero-based `row = 0..6`, `column = 0..4`, the
one-dot word is `1ULL << (row * 5 + column)`. Independent affine fits of
all 35 measured centres in each slot had maximum residual below 0.83
camera pixels and RMS below 0.30 pixels. This supersedes the earlier
unverified slot-1 coordinate assignment for these newly captured data.
Bits 35-39 showed no resolved response in these slots; they are not
therefore proven unused. Slot 0 resolved its previously identified 14
bars and bottom dots in bits 0-14, with no resolved response for bits 15-39.

The portable [partial command map](media/display-command-map.partial.json)
contains every accepted word, observed status, component coordinates,
matrix cells and thresholded optical-core pixel runs. The
[partial bit atlas](media/display-command-atlas.partial.svg) numbers the
observed shapes by bit index; hover for each command. Coordinates are
approximately translation-normalized using the bar references, not a
full camera-geometry calibration. Raw display crops, masks, rejected
attempts and checkpoints remain in the local session under
`display-inventory/inventory-20261113-shaded/`.

**At this stage, the full inventory was incomplete.** Position checks rejected attempts
and capture resumed only from verified block boundaries. The last slot-14
attempt resolved 14 reference bars but failed its final whole-pattern
comparison: aligned similarity 0.896, below the required 0.95. Its word
captures were excluded from the partial map. Slots 14-25 were marked
unverified, not blank or unused. No further brightness increase or
threshold relaxation was used to force acceptance. The lower 1-16 markers
were not yet identified. Output was stopped and camera settings restored.
These are optical observations, not electrical verification, a faint-
ghosting exclusion or a Sony current-rating signoff.

### Revalidation at 3.75% held-grid duty

Later on 2026-11-13, all **560 commands in slots 0-13 passed**
revalidation at 37.5 us CLEAR HIGH / 962.5 us LOW (3.75% duty).
The requested 4% would exceed the existing held-grid limit of 1/26;
the user selected 3.75% instead. The guard was not changed.
All 470 previously resolved responses and 90 unresolved responses matched
the saved map, including component counts and the 5x7 dot ordering.
Maximum registered position difference was 2.10 camera pixels against
a 3-pixel limit. Five-frame consensus and the original emission
thresholds were retained.

Camera autofocus was unsuitable for these shaded captures. Fixed focus
30, 10 ms exposure and gain 8 resolved the calibration patterns. Supported
UVC pan, tilt and zoom controls were exercised; the final setup used
pan -14400 arcseconds (-4 degrees), tilt +3600 arcseconds (+1 degree)
and zoom control 9/100. The fine source crop was
`x=725, y=740, width=2400, height=800`; the display crop within it was
`x=90..2340, y=60..430`. Four known corner dots registered each camera
setup to the saved map. Bar references checked movement between blocks.
When repeated bars confused phase correlation, a centroid-seeded
translation fallback required ECC score >=0.98, aligned correlation
>=0.95 and marker residual <=2 pixels; it did not relax the response
or command-position thresholds.

Validation resumed from accepted boundaries after camera setup changes;
rejected attempts were not counted. Every accepted block ended with
confirmed Stop and a blank optical image. The
[validation summary](media/display-validation-375.json) records all 14
blocks, their camera transforms and comparisons. Raw local evidence is in
`display-inventory/validation-375-20261113/` (slots 0-3) and
`display-inventory/validation-375-20261113-fixed-view/` (slots 4-13).
ESP output was stopped with firmware error 0. Exposure/gain/focus settings
were restored; the final PTZ position was retained. No new inventory
blocks in slots 14-25 were run. This remains an optical validation, not
electrical verification or proof that faint ghosting is absent.

### Complete 26-slot camera inventory

The remaining slots 14-25 were captured on 2026-11-13 at the validated
3.75% duty, fixed focus and PTZ settings. All 480 one-hot commands had
five-frame captures, before/after bar references, confirmed Stop and a
blank post-Stop image. Combined with the retained and revalidated first
560 commands, the inventory now covers **all 26 slots x 40 bits**:
**846 resolved responses and 194 without a resolved response**.

| Slot(s) | Bits with a resolved response |
|---------|------------------------------|
| 0 | 0-14: 14 signal bars and bottom dots |
| 1-22 | 0-34: 5x7 dot matrices, same row-major convention |
| 23 | 0-21: source/LNC and lower numbered markers |
| 24 | 0-14: audio/program/tuning annunciators |
| 25 | 0-23: STEREO/MEMORY and red glyph elements |

The lower **1-16 markers are slot 23 bits 6-21**. For marker
`n = 1..16`, use `1ULL << (n + 5)`; marker 1 is `0x40` and marker 16
is `0x200000`. Each marker is a lit rectangle surrounding its numeral,
as directly visible in the individual camera captures.

Slot 25 bit 16 was too faint to meet the regular three-pixel core
criterion. A controlled follow-up kept display duty at 3.75% and instead
used camera exposure 20 ms / gain 16. The element then resolved in all
five frames; zero-word and adjacent-element controls passed, as did
before/after corner-dot position checks. The full map retains the initial
unresolved status as provenance and uses this confirmed supplemental
response. No detection threshold or held-grid duty guard was relaxed.

The [complete command map](media/display-command-map.json) and
[complete bit atlas](media/display-command-atlas.svg) supersede the
partial exports for current coverage. The first 14 slots are preserved
unchanged. New optical masks are affine-registered to the reference
crop; native component measurements and source-camera transforms are
retained. Original masks in slots 0-13 were captured at 2% and revalidated
at 3.75%; slots 14-25 were captured at 3.75%. Raw new evidence is under
`display-inventory/remaining-375-20261113/`, with the faint-element
follow-up under `display-inventory/red16-sensitive-375-20261113/`.

All commands have been tested, but an unresolved response does not prove
an unused bit. Registration outside the corner-dot field is approximate;
this is an optical command map, not an electrical signoff or proof of
absent faint ghosting. Output was stopped with firmware error 0 after
the sweep and follow-up.

### Physical-reference coverage comparison

The completed camera map was compared with the
[FL701 service drawing](https://github.com/RZomerman/DAR-1000ES/blob/main/media/Screenshot%202026-11-08%20215103.png).
**No functional symbol shown in that drawing remains unmatched to an
observed command response.** The
[annotated comparison](media/display-reference-coverage.svg) shows the
reference alongside a union of the separately captured optical masks.
Hover over reference annotations for their slot, bit and word. The
[coverage report](media/display-reference-coverage.json) records each
correspondence and its evidence.

| Reference comparison units | Matched |
|----------------------------|---------|
| 22 matrices, each with 35 dots | 770 |
| 14 signal bars and 14 bottom dots | 28 |
| Lower numbered markers 1-16 | 16 |
| Red glyph strokes, seven-segment digit and adjacent "1" strokes | 23 |
| Source/audio/program/tuning/status legends and index symbols | 24 |
| **Total comparison entries** | **861** |

These are explicitly defined comparison units, not a count of individual
phosphor islands: a whole word legend counts once. The 861 entries share
846 observed commands because the meter dots share one bit, the adjacent
red "1" shares one bit between its two strokes, and right MONO plus its
index 2 share one bit.

The reference itself independently yielded 22 enclosed matrix outlines
with exactly 35 cells each. Reference and camera matrices were paired by
their left-to-right order, then every dot was uniquely matched within its
local grid; maximum local fit residual was 0.752 reference pixels.
Fourteen enclosed meter-bar outlines were also independently counted.
Other symbols were visually compared with individual captures, including
the numbered pads, all annunciators and the left red glyph. Its faint
centre-left response uses the previously controlled slot-25/bit-16
sensitive-camera confirmation.

The drawing is not a metric phosphor mask. Its outlined cells and printed
numerals differ from luminous dots and negative-contrast pads; the small
red-glyph drawing also joins strokes whose individual boundaries are not
resolved. Coverage is therefore by functional identity and topology, not
global pixel overlap, exact stroke boundaries or uniform brightness.
The 194 unresolved commands are still not proven unused. This comparison
adds no electrical/current-rating or faint-ghosting certification.
It used existing sequential evidence only: no new illumination, brightness
increase or simultaneous all-elements held-grid test was performed.

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
| LNC label | `0x04` | Earlier passive candidate; now confirmed by the one-hot camera sweep |
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
| Left SPEECH | `0x01` | Camera-confirmed |
| Left MUSIC | `0x02` | Camera-confirmed |
| Left MONO index 2 | `0x04` | Passive transition and camera-confirmed |
| Left MONO index 1 | `0x08` | Passive transition and camera-confirmed |
| Left MONO label | `0x10` | Earlier passive candidate; camera-confirmed |
| PROGRAM 3 | `0x20` | Passive transition and camera-confirmed |
| PROGRAM 2 | `0x40` | Passive transition and camera-confirmed |
| PROGRAM 1 | `0x80` | Isolated by PROGRAM 1 -> 2 -> 3 |
| PROGRAM label | `0x100` | Camera-confirmed; formerly unassigned setup-state bit |
| Right SPEECH | `0x200` | Camera-confirmed |
| Right MUSIC | `0x400` | Camera-confirmed |
| Right-hand MONO 2 label | `0x800` | Passive transition and camera-confirmed |
| Right-pointing tuning triangle | `0x1000` | Camera-confirmed |
| TUNED | `0x2000` | Camera-confirmed |
| Left-pointing tuning triangle | `0x4000` | Camera-confirmed |

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
transition implicates its bit in slot 24. The camera sweep identifies
`0x100` as the PROGRAM label, not MEMORY.

## 7. Red display: slot 25

These are complete measured words, including any coexisting indicators.
Individual segment masks are assigned in the live and camera mappings below.

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

### Live red-segment mapping

On 2026-11-12, the user confirmed the following slot-25 single-bit mapping
after a second held-mode sweep at approximately two seconds per bit,
20 us CLEAR HIGH / 980 us LOW (2% duty). All other bits and slots were zero.

| Mask | Observed element |
|------|------------------|
| `0x0000000004` | Red digit: top (a) |
| `0x0000000008` | Red digit: upper-right (b) |
| `0x0000000010` | Red digit: lower-right (c) |
| `0x0000000020` | Red digit: bottom (d) |
| `0x0000000040` | Red digit: lower-left (e) |
| `0x0000000080` | Red digit: upper-left (f) |
| `0x0000000100` | Red digit: centre (g) |
| `0x0000000200` | Larger "1"-shaped element to the left of that digit |

This is visual identification, not an electrical waveform signoff. Other
bits were not identified by that confirmation.

In a subsequent four-bit sweep using the same two-second dwell and 2% duty,
the user reported these smaller red elements. The final identification was
explicitly uncertain; do not treat it as confirmed.

| Mask | Reported element |
|------|------------------|
| `0x0000000400` | Top |
| `0x0000000800` | Upper-right |
| `0x0000001000` | Upper-left |
| `0x0000002000` | Tiny middle-right element (tentative) |

### Camera-resolved left red glyph

The 2026-11-13 camera inventory resolves the 14 elements in bits 10-23.
These positions supersede the earlier smaller-field descriptions above:
in particular, bit 12 is an upper-centre vertical, not the outer upper-left
element; bit 13 is an upper-left diagonal. The camera-view assignments are:

| Bit | Element |
|-----|---------|
| 10 | Top horizontal |
| 11 | Upper-right outer vertical |
| 12 | Upper-centre vertical |
| 13 | Upper-left diagonal |
| 14 | Upper-right diagonal |
| 15 | Upper-left outer vertical |
| 16 | Centre-left short element; confirmed by the sensitive-camera follow-up |
| 17 | Centre-right short element |
| 18 | Lower-right outer vertical |
| 19 | Lower-right diagonal |
| 20 | Lower-left diagonal |
| 21 | Lower-centre vertical |
| 22 | Lower-left outer vertical |
| 23 | Bottom horizontal |

Bits 0-9 retain the previously confirmed STEREO/MEMORY, seven-segment digit
and adjacent "1" assignments. Bits 24-39 showed no resolved response.

### MEMORY mapping

Live held-mode verification on 2026-11-12 identified slot 25 word
`0000000001` (bit `0x01`) as **STEREO**, explicitly confirmed by the user
after a three-second test at 20 us CLEAR HIGH / 980 us LOW (2% duty).
This identifies the indicator; it is not an electrical waveform signoff.

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
MEMORY bit. In the 2026-11-12 held-mode slot-25 sweep (decimal words 1..16,
approximately one second each, 20 us CLEAR HIGH / 980 us LOW), the user
subsequently confirmed that word `0000000002` alone lit **MEMORY only**.
This visually verifies bit `0x02`, alongside STEREO at bit `0x01`.
The user also observed fragments of a red digit during the sweep; no
individual red-segment assignment was established by that report.

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
below; that passive evidence did not establish individual bar transitions.
The later live slot-0 progression above visually verified the cumulative map.

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
