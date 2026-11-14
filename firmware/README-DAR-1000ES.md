# DAR-1000ES internet radio baseline

## Prepared release: 0.3.32

Eight display modes now cycle in order: Now Playing, Stream Source, Audio
Format, BitRate, Station Genre, StreamURL, Equalizer, ESP IP address. Shared
task-context controls are ready for a future debounced Display Mode button;
short press advances one mode, long press cycles 0.25% / 0.5% / 0.75% duty
without changing the mode or scan period. The radio webpage exposes the
same controls. They cannot start stopped output or take over manual display.
The build and 30 host regressions plus both browser checks pass. This release
is not yet installed; the connected device remains on 0.3.31.

## Installed baseline: 0.3.31

AHCT v2 is now the fixed boot GPIO map, avoiding the former return to the
legacy pins after reboot. Display output still starts stopped. Runtime
selection of the original profile remains isolated-only and is not persisted.
This does not make reset or power transitions electrically safe: firmware
still cannot control AHCT /OE.

Spectrum now labels the left bank MONO 1 and the right bank MONO 2; STEREO
still follows the actual source. The webpage includes the connected 0.75%
brightness setting. Full scans batch up to eight refreshes per DMA block
and patch only changing DATA spans. This removes measured 17-39 ms scan
holes: three station captures had worst steady intervals around 8.02 ms
at 128.20 Hz, with no new steady audio errors. Extra buffering adds latency
(Stop measured 0.28 s). A connected camera snapshot confirms all 22 bars and
STEREO/MONO 1/MONO 2/TUNED at 0.75% per-grid duty; subjective flicker
confirmation remains pending.

Option 1 adds a real stereo spectrum: 11 logarithmic bands per channel across
the 22 matrices, seven-row bars with fast attack/smooth decay, selectable on
the radio webpage instead of scrolling NOW PLAYING. The 48 kHz PCM tap is
nonblocking; a separate low-priority worker performs the windowed FFT. Radio
indicators, the GPIO map, authorization and selected scan duty are unchanged.
Installed and checked while isolated: decoded stereo/channel-swap/mono/silence
fixtures pass; a full analyser capture passes the TD timing/model checks at
126.57 Hz. No dropped PCM frames or new steady-playback errors were observed.
The original bars' appearance was confirmed by the user. The 0.3.30 timing
changes were checked while physically isolated, then the connected display
was enabled and its labels/appearance checked by camera. A still image does
not verify temporal stability or electrical/current compliance.

Start with the complete [screen-driver operating guide](../13-screen-driver-operating-guide.md).
Radio is now enabled with MP3/AAC/ICY metadata, S/PDIF on GPIO42 and an
explicitly authorized 22-character NOW PLAYING display. The homepage is
the radio; `/screen` retains the editor and timing laboratory. Long titles
scroll on the ESP. Display output remains stopped on boot and pin-profile
selection now defaults to AHCT v2.

Radio-owned fixed indicators now show source STEREO/left MONO, bitrate bars,
buffering dots, TUNED and explicit station-genre MUSIC/SPEECH. The GPIO map
and tested brightness are unchanged. NPO's Mixed genre deliberately leaves
both genre labels off. The network-task stack and decoder/display scheduling
were corrected after extended station-switch tests; see the operating guide
for results and limits. Source changes are not guaranteed gapless.

**STB must reach both IC702 and IC703.** After correcting that wiring,
0.3.25 rendered clean 22-matrix text and the existing nine-character marquee.
Working dim is 0.5% per grid / 13% total CLEAR duty, not 3.75% total:
ON39,000 ns, OFF123,500 ns, 300 us slots with the guarded settings in the
operating guide. Physical voltage/current and audible audio remain separate
checks. The sections below retain the earlier development history.

## Historical screen-only baseline

Version 0.3.25 is a screen-only logic bench profile, based on ESP-Streamer at
`493b1a9360d078da92d48095b8e5e6c346244471`. Radio/AirPlay startup, I2S,
S/PDIF and all previous mute/relay/serial assignments are disabled. The
webpage offers an editable dimmed display driver and finite captured replay using the words
documented in [Direct Display driving](../06-direct-display-driving.md).

| Signal | V1 original / TXS0108E | V2 / SN74AHCT |
|--------|------------------------|--------------|
| FLGDATA | 4 | 2 |
| FLGCLK | 5 | 1 |
| FLCLEAR (active LOW blanking) | 6 | 40 |
| FLSTB | 15 | 38 |
| FLSDATA | 16 | 21 |
| FLSCLK | 7 | 39 |
| OE | External, active HIGH | External; AHCT125 /OE active LOW |

V2 grid pins were swapped in 0.3.25 to match the measured bench wiring:
GPIO2 reaches the existing GDATA/D0 test point; GPIO1 reaches GCLK/D1.
The original profile and the other four V2 pins are unchanged.

The webpage's **Hardware / GPIO profile** selector preserves both pin maps
without changing the existing scan, held, alternation or captured-replay
implementations. Selection is accepted only while output is stopped and
`bench_confirmed:true` confirms all six Sony signal wires are physically
disconnected. API: `{"pin_profile":"ahct-v2","bench_confirmed":true}`; use
`"original"` to switch back. Do not combine this with an output command.
The inactive profile's pins become inputs without internal pulls; the active
pins take CLEAR/DATA/STB LOW and clocks HIGH. Switching does not start output.
Status reports `pin_profile`, `pin_profile_name` and the active `pins` array,
which also supplies the webpage's individual voltage controls.

Firmware 0.3.31 and later boot with **AHCT v2**, with output stopped. A runtime
V1 selection is not persisted. Disable AHCT outputs in hardware through
reset/programming and check V2 idle levels before reconnecting/enabling.
Firmware does not control OE on either profile. GPIO12-14 remain disconnected
DMA helper pins for both profiles. New V2 timing at the buffered outputs must be
checked with the analyser before reconnecting Sony.

These mappings do not change the previously designed
`DevBoard/S3-AHCT-Carrier` PCB retains the **old** mapping and is incompatible
with this firmware. Do not use its Gerbers for the new wiring.
TXS0108E OE is hardwired to 3.3 V (VCCA), never 5 V. VCCA is 3.3 V, VCCB
is 5 V, with common ground. This is not active-LOW HCT245 or AHCT125 OE wiring.
Firmware initializes the signal GPIOs to DATA LOW, CLEAR LOW, STB LOW and
clocks HIGH, then holds those driven idle levels after stop/error. Hardwired OE
also enables the translator during reset, before ESP GPIOs are configured;
external signal biasing is needed for guaranteed levels during that interval.
No scan starts on boot. This is not a hardware blanking fail-safe.
Static GPIO tests drive the translator outputs. The new mapping is untested:
reflash only with the Sony signal wires disconnected, then validate on the
isolated analyser before considering live use.

After firmware initialization, the selected six signal pins are outputs at idle levels.
Individual HIGH/LOW webpage buttons drive the active profile for disconnected-buffer
voltage tests. Static levels persist until changed or a scan runs. The Stop
control restores DATA LOW, CLEAR LOW, STB LOW and clocks HIGH. Status shows
commanded logic levels, not measured voltage. Static changes are rejected while
a scan runs; firmware cannot control or measure OE.
The captured-blink button expands the original six-line capture into PSRAM
and sends one 130-frame, 1.017-second cycle with 8 MHz parallel LCD DMA,
including both blink phases and recorded CLEAR/STB intervals. GPIO12
is an unused data lane, GPIO13 is WR and GPIO14 is D/C; leave GPIO12-14
disconnected from Sony.
The transfer requires approximately 8.14 MB of contiguous free PSRAM.
Allocation failure prevents transmission and reports an error.

Before the capture, a 324-microsecond prefix holds grid CLEAR LOW and
STB HIGH while 40 deliberate shifts preload the initial segment word and
grid OUT1. This replaces unknown register contents. Clock data lanes are
inverted in memory and at the GPIO matrix so peripheral idle zero yields
physical clock HIGH. During bus creation these lanes use unconnected
GPIO12; the actual Sony clock pins are attached only after setup.

A 20-microsecond suffix, plus DMA alignment padding, holds CLEAR LOW,
STB LOW and both clocks HIGH. Clock GPIO ownership is restored before
peripheral deletion, without an extra shift. A Stop request during replay
waits for completion and retains the driven idle levels with OE HIGH.

Three buffered analyser captures (Frequency twice, blink once) verified
the 40-shift initialization under CLEAR LOW, grid preload, all comparable
scan words/markers and no extra clock edges at shutdown. Actual live display
operation remains unverified. The older approximate `Frequency`/`No DSR` API remains available
for disconnected regression tests, but is no longer offered on the webpage.
Captured blink also accepts an explicit `live_confirmed:true` for the user's
risk-accepted live experiment; this does not authorize static controls or
the approximate scans. The webpage provides a separate live checkbox.
`Captured Frequency` uses the same authorization and DMA implementation,
but replays 128 consecutive frames from `sony-td-satellite-lnc-off.bin`
(1.001940125 seconds). The extraction checks the nine Frequency glyph words
in every frame and preserves the full original scan, including indicators.
It is a different original acquisition, not a synthesized text sample.
Both captures now have the same explicit initialization/blank-stop wrapper.
Neither replay has produced a verified correct live display result.

`MONO 1/2 test` replaces only segment DATA in the captured blink timing:
slot 24 is `0x08` for 65 frames (~509 ms), then `0x04` for 65 frames;
all other segment words are zero. It uses the same grid scan, initialization,
blank-stop wrapper and explicit bench/live confirmation. This is generated
diagnostic data, not an unchanged Sony acquisition. The command is installed,
but its analyser validation is blocked by incomplete probe signals (no STB
edges in the retry recording). Do not interpret firmware completion as a
verified MONO-only display result.

**Keep Sony disconnected:** use a logic analyser first. High-impedance ESP
pins can leave buffer inputs floating. A hardware output-disable/blanking
mechanism and verified driver CLEAR/STB behavior are required before live
VFD use; software zero flushing does not protect against CPU stalls, power
loss or incorrect wiring. Keep probes off the high-voltage VFD outputs.

Build with `pio run -e radio-s3`; the environment name is retained but its
default profile is now screen-only. Existing network settings are retained.
`data/www/screen.html`, `data/www/display-editor.js` and
`data/www/display-reference.png` must be deployed
alongside the application, or with a matching filesystem image during USB
deployment.

## Editable continuous dim display

### ESP-local held alternation (0.3.22)

Set `hold:1`, `pair:0`, `slot:22` and optional `alternate_ms:50` to alternate
slots 22 and 21 locally without HTTP commands for each switch. Supply both
glyphs in the usual 26-word array. `alternate_ms` defaults to zero (disabled);
enabled dwell targets range from 5 to 1000 ms, with a first slot of 1..25.
Changing timing through HTTP restarts the scheduling phase on the first slot.
The webpage exposes this mode and a 50 ms preset; presets do not start output.

This reuses `sony_scan_submit_timed` and its existing held preload: blank and
drain CLEAR DMA, load the selected 40-bit segment and one-hot grid words once
through GPIO, latch, then resume CLEAR-only DMA. It does NOT use either
failed fast pair backend. Alternation limits queued PWM blocks to one PWM
cycle each instead of the usual batch, reducing drain latency without changing
the requested ON/OFF duty. Normal held mode retains its original batching.

Dwell is a target, not guaranteed pin timing. Status includes
`active_held_slot`, `held_switches` and `held_last_interval_us`, measured around
successful firmware submissions, not at the Sony inputs. Reload blanking,
DMA draining and scheduling can extend or skip requested phases at short
intervals; use an analyser before claiming physical refresh rates.
No scan starts at boot, and Stop still blanks/drains the engine.

In 0.3.23, optional `alternate_blank_steps:3` adds three fully blank steps
after the two grids: `[1 / 2 / blank / blank / blank]`. Each step uses
`alternate_ms`, so a 5 ms dwell targets a 25 ms sequence (40 Hz), including
15 ms continuously blanked. Allowed blank count is 0..20; default zero
preserves two-grid alternation. Blank steps require enabled local alternation.
The driver disconnects CLEAR from DMA, holds it LOW and drains queued PWM
without freeing the engine. Resuming forces the normal held preload before
reconnecting CLEAR. No grid/segment clocks or latches are sent during the
blank phase. Scheduling and drain delays still affect physical durations.
The webpage exposes and saves the blank count. Brightness and per-digit
refresh decrease; this is a ghosting diagnostic, not an electrical signoff.

### Explicit two-grid diagnostic (0.3.20 / GPIO backend in 0.3.21)

Timing requests may include optional integer `pair:1` (default `0`).
With `hold:0`, the driver alternates `slot` and `slot-1`, reloading all 40
segment bits and all 40 grid bits separately, under CLEAR LOW, before each
latch. Other slots remain unselected. `slot` must be 1..25. Per-grid duty is
capped at 1/26; this software bound is not a verified glass current rating.
The webpage exposes this as "Two adjacent grids, explicit full grid reload".
Existing scan and held-mode requests retain their behavior when `pair` is absent.

The initial diagnostic uses slots 22/21 with 1/2/2 us setup/clock-low/high,
5 us strobe and blank/latch guards, one 20 us ON pulse and 2060 us OFF.
Each slot takes 2500 us, giving a nominal 200 Hz pair refresh and 0.4% duty
per digit, excluding blank DMA gaps/padding. This is ESP-local sequencing,
not HTTP-timed alternation. Live position jumps in the normal 26-slot scan
remain unresolved; successful host tests are not connected electrical validation.

The first Sony-connected 0.3.20 pair run (three seconds, no analyser) was
reported as "all over the place", not the intended two digits. Output was
stopped. Explicit full-grid reload therefore has NOT fixed connected
multiplexing. Earlier one-grid held tests, including HTTP-stepped positions,
were visually correct. Those use GPIO preload with CLEAR-only DMA; pair mode
instead uses six-line DMA. This difference is a diagnostic lead, not proof
that DMA or any one hardware component is responsible. Capture connected
logic signals before further fast live tests or brightness increases.

Version 0.3.21 replaces the pair mode's six-line DMA output with GPIO
execution of the same tested waveform. STB remains LOW during shifting, so
the previous output latch is held. CLEAR remains LOW while both complete
40-bit words are loaded separately and STB is pulsed HIGH then LOW; only
then is CLEAR pulsed HIGH for illumination. The IC supports loading the
next word with the previous latch held (datasheet pages 3 and 9); this
diagnostic additionally blanks throughout loading to limit exposure.

Only the short ON interval is protected against ordinary task/interrupt
preemption. GPIO timing overhead and a blanked one-tick task yield after each
waveform reduce actual refresh below the displayed nominal estimate.
Pair edge/ON/OFF settings must be whole microseconds; ON is capped at 50 us.
The host regression executes the GPIO routine with mocked pin writes/delays,
decodes the two one-hot grid/segment patterns, and checks stop/write-error
blanking. These checks do not prove physical timing or electrical safety.
Normal 26-slot and held/CLEAR-only DMA modes are unchanged.

The connected 0.3.21 GPIO-pair test and its repeat were also reported as
"still going over all segments". Both were stopped; no analyser was connected.
Changing from six-line DMA to GPIO did not resolve the fault, so six-line DMA
alone is not an adequate explanation. Do not treat either pair backend as
working live multiplexing; connected clock/STB/CLEAR capture is still needed.

### Runtime timing laboratory (0.3.18)

Version 0.3.17 includes two fixes found with the isolated translator connected
to a 24 MS/s analyser: preload the segment/grid registers with separate clock
edges, and physically take CLEAR LOW before draining queued DMA and handing
back the other GPIOs. The former removed observed preload CLEAR/DATA spikes;
the latter removed a stale-word exposure at shutdown. See
[Chapter 12](../12-td62c950rf-timing-revalidation.md) for measurements and the
remaining isolated clock-dropout anomaly. This is not a live-Sony safety signoff.

Subsequent connected tests on 0.3.17 showed extra clocks and STB events;
a single T moved to other grids and illuminated unrelated elements.
Version 0.3.18 therefore changes held mode to a GPIO-only preload followed
by CLEAR-only DMA: the other five Sony pins never connect to the LCD peripheral
in this mode. This change does not establish an electrical root cause or
approve connection to the Sony. Its first isolated capture read all eight
analyser inputs HIGH; after the user reset the shifter's 5 V supply, valid
24 MS/s captures verified exactly 40 segment clocks, 40 grid clocks and one
STB pulse for a three-second T, followed by CLEAR only. Brightness changes
did not resend data; word/slot changes each added one preload. Mode changes
and minimum GPIO delays also passed isolated checks.

A subsequent powered T was reported visually "absolutely perfect", but its
connected capture recorded 163 STB rises rather than one and CLEAR glitches.
An explicitly authorized dim TEST scan also retained severe capture
anomalies. Both runs were bounded and stopped. The GPIO redesign is not
a connected-load electrical signoff; probe mapping, analogue thresholds,
contention and power/ground integrity still require investigation.

The root webpage now defaults to a new **generated** waveform driver. The
legacy captured/resampled driver and finite capture replays remain available.
Stop before changing between the generated and legacy backends.
No output starts at boot, when loading the page, loading a preset or loading
browser-saved settings. Timings are kept in RAM by the ESP; Save/Load stores
the form in this browser without restoring output after a reboot.

Generated scanning blanks CLEAR, shifts 40 segment bits MSB first and one
grid bit with STB LOW, pulses STB HIGH while still blanked, then exposes
that slot using CLEAR. The grid marker repeats every 26 shifts, matching
the captured topology; the upper 14 shift-register stages are not independent
display slots. This is a new sequencing experiment, not an original capture.
The 26 words and glyphs are the existing editor/catalogue values.

Two experiments are available:

- **26-slot scan:** send each slot once, then issue 1..16 CLEAR HIGH/LOW
  cycles before advancing. One pulse gives `[blank/send/latch/ON/OFF]`;
  multiple pulses give `[send/latch/ON/OFF/ON/OFF/...]`.
- **Held grid:** preload the selected grid and its 40-bit word once, latch
  with CLEAR LOW, then send only CLEAR pulses. Only CLEAR is connected to
  DMA; DATA is held LOW, clocks HIGH and STB LOW through static GPIO.
  Editing the selected slot or its word blanks first, drains pending DMA,
  and reloads once. Brightness or edge-timing changes do not reload the word;
  stop/start explicitly reloads it. The two ICs cannot retain 26 independent segment words:
  holding a grid is a single-field test, not a static full-screen mode.

Controls expose DATA setup, CK LOW/HIGH (HIGH also provides DATA hold),
STB HIGH, CK-to-latch guard, blank-before/after, CLEAR ON/OFF, pulse count,
mode and held slot. Values are exact multiples of the 125 ns DMA sample.
Held-mode preload delays are rounded up to whole microseconds, plus GPIO
overhead; interrupts may extend them. Edge settings apply at the next
preload. CLEAR PWM retains 125 ns sample resolution.
Edge settings are limited to 250..20000 ns, blank guards to at least 2 us,
ON to 250 ns..280 us, OFF to 2..5000 us, and each DMA block to 512 KiB.
Held-grid duty is limited to 1/26 (`OFF >= 25 * ON`). These application-level
guards are **not a manufacturer VFD-glass rating or hardware fail-safe**.

The 301 us and 154 us presets reproduce those nominal slot intervals, not
the original Sony phase relationships. The four-pulse preset uses 20 us
ON / 20 us OFF; the held-grid preset uses 50 us ON / 1450 us OFF (3.33% duty).
Presets do not transmit. Use MEMORY-only to clear the local editor and set
slot 25 to `0000000002`, select the appropriate preset and explicitly
confirm bench/live wiring before Apply.

POST `/api/screen` with `sample:"Timing"`, 26 hexadecimal `words`, explicit
bench/live confirmation, and a complete `timing` object:

```json
{
  "setup_ns":250, "clock_low_ns":500, "clock_high_ns":500,
  "strobe_ns":3000, "latch_guard_ns":2000,
  "blank_before_ns":2000, "blank_after_ns":2000,
  "on_ns":50000, "off_ns":190750, "pulses":1, "hold":0, "slot":25
}
```

All fields must be JSON integers. Invalid settings are rejected without
changing the running waveform. Updates snapshot timings and words together
and apply at DMA transaction boundaries, behind up to two queued blocks.
Status distinguishes desired and queued timing revisions; queued is not a
physical-pin measurement. Generated blocks target roughly 10 ms (at least
one whole frame, up to eight cycles). Padding holds CLEAR LOW for up to
7.875 us and software/peripheral transfer gaps can add blank time.
Nominal frequency/duty excludes these gaps; a full frame can approach
65 ms at extreme settings. Completed cycles mean full frames for scanning,
or PWM cycles for held-grid mode; interrupted final blocks are not counted.

The datasheet page-5 recommended operating limits are 8 MHz maximum CK,
75 ns minimum CK pulse width and 50 ns minimum DATA setup/hold across the
stated operating temperature range. Page-7 switching minima are 70 ns CK/STB, 20 ns DATA setup/hold and
50 ns CK-to-STB setup, at the stated 5 V/70 V test conditions. The new
generator uses at least 250 ns for edge timing and 2 us blank guards for
the listed output propagation delays. This removes capture resampling and
its occasional single-sample pulses, but does **not** resolve the earlier
physical STB anomaly by itself. Verify HIGH >= 3.5 V / LOW <= 1.5 V at
5 V VDD, rise/fall <= 1 us, CHG wiring, actual CK/STB pulses and brightness
with appropriate logic-side instruments. Do not infer physical compliance
or flicker-free output from a successful API response.

Isolate the Sony signal wires for USB reset/reflash or when its logic supply
is off. Page 5 limits input voltage to VDD + 0.5 V absolute maximum:
5 V translator outputs must not drive unpowered TD62 inputs, including the
clocks that idle HIGH after Stop. Keep OE HIGH in the current fixture and leave
GPIO12-14 disconnected from Sony. Stop returns CLEAR LOW and the normal
idle levels; power removal or CPU/DMA failure still needs hardware protection.

### Legacy editor and resampled driver

The page provides 22 left-to-right character references P01-P22, each labelled
with its decoder slot (22 down to 1). Message slots 22-14, frequency slots 10-6
and MHz slots 5-3 are editable by character or individual 5x7 pixel. Five
unassigned slots are displayed but disabled; their physical positions in the
complete glass layout have not been verified. The generated upper/lowercase
font is not a complete original Sony font.

Standard controls expose confirmed PROGRAM selectors, left MONO indices,
right MONO, MEMORY, CABLE/SATELLITE and LNC selections. Candidate label bits
are visibly marked `(?)`. Red A/B/C/D preserve the independent MEMORY bit.
Signal off/startup-dots/full use captured words; individual meter elements,
STEREO, MUSIC/SPEECH, TUNED/arrows and lower numbered markers remain disabled.
The meter is a schematic full-scale preview, not an individual segment map.
All 26 word codes are shown for inspection.

The colorized glass overview arranges the labelled elements, red field,
22 character references and signal meter together. Unknown labels, lower
markers and meter elements are selectable for inspection without changing
any word. The red preview uses a representative letter, not a verified
physical segment drawing; numbered red-bit controls expose captured masks.
The layout follows the overlapping original glass drawings, with one shared
TUNED group. Per the user's hardware clarification, the red field is drawn
as a small letter matrix followed by two seven-segment numeric digits.
These shapes are clickable for inspection, not assigned guessed bit positions.
The letter preview uses the application font as an illustration; it is not a
verified pixel rendering of the red letter's actual wiring.
The original `media/Screenshot 2026-11-08 215103.png` is served locally as
`/display-reference.png`, color-tinted beneath the clickable hotspots.
The screenshot is the physical-layout reference; hotspot placement does not
establish unknown electrical mappings.

Individual-item validation offers manual next and a finite one-second-per-item
cycle for indicators, red bits, red A/B/C/D presets, a selected character's
35 pixels, or captured meter states. Each step sends only the selected mask,
with all other slots zero, and shows its name, slot and code for visual
comparison. Candidate indicator assignments remain explicitly marked.
Automatic cycles blank on completion; Stop cancels future steps, waits for
an in-flight request, then requests active blanking. A lost browser/network
connection cannot guarantee stopping the ESP, so retain physical power access.

Edits stay local until Apply. POST `/api/screen` with `sample:"Display"` and
`words` containing 26 strings, each exactly ten hexadecimal digits, starts
or updates the generated scan. `bench_confirmed:true` or
`live_confirmed:true` is required for every Apply. No desired frame or running
state persists across reboot; no output starts merely by opening the page.

The persistent driver decodes the first eight complete Frequency fixture frames
once, caches bright/dim templates, and allocates two reusable DMA buffers.
The peripheral is configured once per scan. It queues one buffer while the
previous buffer transmits; completion notifications protect buffer ownership.
Only DATA spans are patched for new words. To reduce visible dim flicker, the
display path scales each captured approximately 301 us slot to 154 us
(approximately 250 Hz frame refresh), including scaling the 201 us dim CLEAR
delay to about 103 us. This deliberately departs from the captured timing.

The first approximately 32 ms DMA transfer preloads the registers while
blanked. Subsequent transfers continue the existing scan stream without
repeating the preload or blank suffix; only DMA alignment padding (under 8 us)
holds the final waveform state. Earlier direct GPIO21 captures of the old
repeated-preload path showed 0.60-0.68 ms boundary blanking, rather than the
previous approximately 148 ms gaps. Live analyser captures of the 0.3.12
approximately 128 Hz scan measured maximum FLSCLK rising-edge intervals of
203-283 us, with stable MEMORY data, but the display still visibly flickered.
The faster 0.3.14 timing requires fresh analyser and visual validation.
Updates are coherent at transfer boundaries and may wait behind queued data.

`sample:"blank"` requests stop and retains CLEAR LOW. `sample:"stop"` waits for
the current DMA chunk and restores the driven idle levels; OE remains hardwired.
Software blanking is not a hardware fail-safe against CPU stalls, missing DMA
completion, power loss or buffer faults.

Host waveform checks cover every generated slot's word, clock/grid count,
dim-enable timing and preservation of other captured signals. The firmware
and editor have not yet been deployed or validated on the connected Sony.
Use the replacement buffers and a disconnected analyser bench before live use.

## Native text and scrolling API

Application code can treat the VFD as a nine-character text display:

```c
#include "sony_vfd.h"
esp_err_t err = sony_vfd_show("Now Playing: blabla", true, true);
/* Arguments: UTF-8 text, scroll enabled, dim enabled. */
```

Start an explicitly authorized `sample:"Display"` scan first. The text API
never starts output or authorizes the wiring; it returns
`ESP_ERR_INVALID_STATE` when the display is stopped or running another mode.
It changes only message slots 22 down to 14, preserving all other words.
New text replaces the current title atomically. Identical rendered text with
the same scroll flag does not restart the scrolling timer.

Titles of up to nine display characters stay static. Longer titles pause
one second on their initial window, then move one cell every 300 ms, with
four blank cells between the end and beginning. Each loop pauses again at
the first window. `scroll=false` shows the first nine cells without motion.
Empty text clears the message field.

The HTTP equivalent is POST `/api/screen`:

```json
{"sample":"Text","text":"Now Playing: blabla","scroll":true,"dim":true,"live_confirmed":true}
```

Only confirm live output after verifying the wiring and isolation; an isolated
bench can instead use `bench_confirmed:true`. Scroll/dim default to true and
must be JSON booleans when supplied. Status reports `scrolling` and brightness.
`dim=true` uses the measured delayed CLEAR enable; `dim=false` retains the
bright fixture's original CLEAR waveform. Chunk boundaries remain blanked in
both modes. Raw Display updates take manual control and cancel scrolling.

The native font is generated from the same Sony/custom editor dictionaries
by `node scripts/generate_display_font.js`. Native lookup recognizes the
listed composed/decomposed accents, groups combining marks, presentation
selectors, emoji modifiers, ZWJ sequences and regional-indicator pairs, and
uses the documented base-letter or `*` fallback. This is not a complete
Unicode shaping/bidi or grapheme-break implementation. Strict UTF-8 validation
rejects malformed text; maximum input is 1,024 bytes and 256 display clusters.
Rejected requests preserve the previous title. JSON bodies remain limited to
2,047 bytes, so heavily escaped text may reach that limit first.

ICY title updates call this API when the Sony screen feature is compiled;
they never start a stopped display. ICY metadata is not guaranteed UTF-8:
malformed/non-UTF-8 titles are rejected rather than guessed. Radio generation
changes and new display titles do not require a browser connection.
The current screen-only profile does not enable radio playback.

This firmware has been deployed to the isolated ESP and measured directly on
its GPIOs, but the faster refresh has not yet been validated on the connected
Sony. Refresh uses reusable, queued approximately 32 ms DMA chunks;
text timing is applied at chunk boundaries, not at an exact 300 ms hardware
deadline. A fully gap-free scan backend is separate future work.

## Previous radio bench profile (0.2.7; no longer active)

Version 0.2.7 was based on the known-good ESP-Streamer radio firmware at commit
`493b1a9360d078da92d48095b8e5e6c346244471`. The committed baseline has no
Sony RDS implementation; the separate, experimental RDS and Sony bench-control
changes were not included. Radio metadata remains available in the web UI.

S/PDIF is on GPIO42 at 48 kHz. A separate I2S controller mirrors the PCM
stream as 48 kHz, stereo, 16-bit Philips I2S: LRCK GPIO7, DATA GPIO8 and
BCK GPIO9, with no MCLK. This is a bench format: the Sony input format,
clocking and compatibility have not been verified. The two output
controllers are not guaranteed to be phase-aligned.

Manual webpage controls drive D.Mute GPIO10
and A.Mute GPIO11 LOW at startup, and permit a manual HIGH request. Relay
GPIO12 starts high-impedance with internal pulls disabled, until explicitly
set LOW or HIGH; its active polarity is unknown. Status reports commanded
ESP levels, not the actual voltage at the Sony. These defaults apply once
the application initializes, not throughout reset or before it starts.
Isolate the original Sony drivers before connecting these outputs.

INIT GPIO2, ATT GPIO4, SHIFT GPIO5 and LATCH GPIO6 start high-impedance,
with no internal pull. They can be set manually. A confirmed button press
replays the captured long LATCH pulse, unlatched 96-clock burst, two latched
16-clock words, and first unlatched 24-clock burst. The later recurring
24-clock traffic is not replayed. INIT must be manually HIGH and both mutes
LOW; no replay occurs automatically. Timings are approximate, referenced to
the button press rather than the INIT transition, and are not validated on
the output pins. Command meanings, the unknown burst's destination, and
the Sony's response remain unverified. Do not drive an unpowered receiver;
compare waveforms with the original drivers isolated before connecting it.

Initialize the pinned dependency and build:

```sh
git submodule update --init --recursive
cd firmware
pio run -e radio-s3
```

The application can be updated via `/api/ota/update` only on a device already
using the compatible radio partition layout. The OTA update reboots the unit
but does not update the web filesystem or erase Wi-Fi settings. Read
[README-RADIO.md](README-RADIO.md) for partition layout, recovery, and
filesystem update details. Do not use the generated `sdkconfig.radio-s3` from
another build: it can override the checked-in defaults.
