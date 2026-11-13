# Sony DAR-1000ES screen driver: wiring, operation and radio integration

This is the current operating guide for the ESP32-S3/SN74AHCT125 display
driver. It supersedes the older experimental operating advice, not the
historical measurements, in chapters 6, 11 and 12.

On 2026-11-13, firmware 0.3.25 produced clean held characters, mixed text
across all 22 matrices and an ESP-local marquee. The decisive wiring
correction was connecting **STB to both TD62C950RF chips**, not just one.
Firmware 0.3.26 integrates internet radio and opt-in, full-width NOW PLAYING.
Firmware 0.3.28 adds channel, bitrate, playback and station-genre indicators.
Firmware 0.3.29 adds selectable stereo spectrum visualization, installed and
verified with isolated digital captures and decoded tone fixtures. Optical
appearance was subsequently confirmed by the user at 0.75% per-grid duty.
Firmware 0.3.30 adds MONO 1/2 spectrum-bank labels and longer DMA buffering
after music exposed visible scan jitter; its connected optical recheck is pending.
Electrical/current certification is still outstanding.

## 1. Hardware and signal ownership

- ESP32-S3 N16R8: 16 MB flash, 8 MB PSRAM.
- IC702 drives grids; IC703 drives segments. Both contain a shift register
  and a separate output latch.
- Use two SN74AHCT125N packages for six unidirectional 3.3 V to 5 V signals.
  Supply the AHCT devices at 5 V, with common ground and local 100 nF
  decoupling at each package. Keep signal/return wiring short.
- Each AHCT channel has its own **active-LOW `/OE`**. Firmware neither
  controls nor measures these enables. Disabled outputs are high-impedance,
  not guaranteed LOW; hardware biasing/blanking is a separate requirement.
- Isolate the original Sony controller's drivers before connecting ESP
  outputs. Do not join two driven outputs together.

### Final measured AHCT v2 allocation

| Signal | ESP GPIO | Analyser channel | Destination |
|---|---:|---|---|
| GDATA | 2 | D0 | IC702 grid serial input |
| GCLK | 1 | D1 | IC702 grid clock |
| CLEAR | 40 | D2 | IC702 grid CL, LOW blanks |
| SCLK | 39 | D3 | IC703 segment clock |
| STB | 38 | D4 | **STB inputs of BOTH IC702 and IC703** |
| SDATA | 21 | D5 | IC703 segment serial input |

Use the **same buffered STB output** for both latch inputs. No second ESP
GPIO is needed. A clean capture at the buffer does not prove that the signal
reaches both physical TD inputs.

The original selectable profile remains GDATA4, GCLK5, CLEAR6, SCLK7,
STB15, SDATA16. Its TXS0108E OE was active HIGH; do not apply that enable
rule to an AHCT125. V2 grid GPIOs were corrected in 0.3.25: **GPIO2 is
GDATA and GPIO1 is GCLK**, regardless of earlier proposed allocations.

GPIO12-14 are internal LCD-DMA helper signals and must remain disconnected
from the Sony. GPIO42 is the radio's separate, 3.3 V S/PDIF output.
GPIO19/20 remain native USB; GPIO48 is the board's addressable LED.
The older carrier PCB is not the final v2 wiring reference.

### TD input states and numbering

Verify segment CL HIGH and both CHG inputs LOW. CL LOW overrides outputs
without clearing the shift register. CHG HIGH with CL HIGH can force all
outputs HIGH and is not a normal operating state.

Do not confuse Sony drawing pin labels with manufacturer package numbers:
the drawing and Toshiba RF numbering are reversed (`manufacturer = 61 -
drawing`). Manufacturer STB35 corresponds to Sony drawing26; CL37 to24;
CHG25 to36; CK27 to34; serial-in32 to29. Identify the package orientation
and check continuity before selecting a physical test pad.

Never attach a low-voltage logic analyser to the high-voltage VFD outputs.

## 2. Safe connection, restart and disconnection

**Sony OFF is not electrical isolation.** Enabled 5 V AHCT outputs can
backfeed an unpowered TD input.

1. Stop output in the webpage. Verify `/api/screen` reports
   `running:false` and `last_error:0`.
2. Disable AHCT outputs in hardware: `/OE` HIGH.
3. Power the Sony off before changing wiring. Physically disconnect all
   six signal wires before flashing, resetting or unplugging the ESP.
4. With the signals disconnected, boot/program the ESP. Boot selects
   **the original profile**, not AHCT v2, and starts no display output.
5. In `/screen`, confirm the disconnected bench and select `ahct-v2`.
   Check its six idle signals before reconnecting.
6. With outputs disabled and Sony off, make connections and check that
   the original controller remains isolated, including both STB routes.
7. Power the Sony's logic before enabling AHCT outputs. Enable `/OE` LOW
   only when the chosen pins and idle levels are verified.
8. Start a low-duty display test. Keep a physical cutoff available.

Do not reset or unplug the ESP while connected: the nonpersistent profile
returns to the original GPIOs. Software Stop is not a hardware fail-safe
for a stalled CPU, floating inputs, power loss or incorrect wiring.

Idle, in analyser order D0..D5, is `0,1,0,1,0,0` (`0x0A`):
DATA LOW, clocks HIGH, CLEAR LOW, STB LOW.

## 3. How the generated driver works

The normal full scan visits **26 grid slots**, including blank words:

```text
CLEAR LOW
  -> shift 40 segment bits with STB LOW
  -> shift the next grid bit
  -> settle, STB HIGH then LOW
  -> settle, CLEAR HIGH for ON
  -> CLEAR LOW for OFF
  -> next slot
```

CK rising shifts. STB HIGH is transparent; STB LOW holds the output latch.
The driver shifts while the previous latch is held and all grids are
blanked. STB falling closes both latches before CLEAR permits illumination.
The segment CL is not driven by this external CLEAR wire.

The native font uses seven rows of five bits, `bit = row*5 + column`, in
the low 35 bits of a 40-bit word. Matrix slots run left to right **22 down
to 1**. Slots 0,23,24,25 are fixed-element fields, not extra text cells.
See the [bitmap catalogue](10-display-bitmap-catalogue.md) and
[element guide](08-esp-display-driver-specification.md) for individual
words and indicator combinations. No word-array rotation is required
after the shared-STB correction.

Full timed scans now batch up to eight identical refreshes in each DMA
transfer. At the guarded 300 us slot settings this is 62.4 ms per block,
not a slower display refresh: all 26 slots still repeat every 7.8 ms.
This gives software time to refill the DMA queue during audio/resampler
bursts. Content-only changes patch SDATA in completed buffers rather than
rebuilding the clock/CLEAR waveform. Held and GPIO-pair batching is unchanged.
Queued blocks add visual-update/Stop latency; measured Stop was 0.28 seconds.
Keep the physical output-disable available.

### Modes

| Mode | Purpose |
|---|---|
| Generated 26-slot scan | Normal full-screen text, NOW PLAYING or stereo spectrum |
| Held grid | Load one 40-bit segment and 40-bit grid word once, then CLEAR-only PWM |
| Repeated CLEAR | Multiple ON/OFF pulses after each load; use `pulses` |
| Held alternation | Diagnostic: locally alternate two held grids with optional blank steps |
| GPIO pair | Historical diagnostic, not the recommended text backend |
| Legacy/captured replay | Historical experiments; do not assume they use the selected dim duty |

Holding one grid does not store a visible full screen. Other grids remain
off. In held mode, changing brightness alone does not resend words;
changing the selected word or grid does.

## 4. Correct brightness calculation

Let:

- `N` = number of scanned slots, not the number of nonblank characters;
- `p` = CLEAR pulses per slot;
- `Ttransfer` = blank-before + serial transfer + latch/STB/after guards;
- `Tslot = Ttransfer + p*(ON + OFF)`.

Then:

```text
frame rate       = 1 / (N * Tslot)
total CLEAR duty = p * ON / Tslot
per-grid duty    = p * ON / (N * Tslot)
```

For held mode, no load is repeated each PWM cycle, so its steady-state
duty is `ON/(ON+OFF)`. Reload/alternation blank time reduces real averages.
DMA initialization, padding and scheduling also slightly reduce measured
rates relative to the nominal calculation.

**The current normal scan always has N=26.** One letter and 22 letters
therefore need the same ON time for approximately the same brightness
per illuminated dot. Do not multiply duty when more characters are added
and do not divide it by the number of lit glyphs.

More lit dots do increase aggregate load. A useful *logical load proxy*
is `per-grid duty * sum(popcount(word))`; this is not amperes. Grid current,
segment current, filament voltage and supply droop require actual
measurements, not a dot-count-derived claim of electrical safety.

### Tested working presets

All four use 300 us slots and a nominal 128.205 Hz full scan. Earlier images
measured approximately 127.8 Hz; 0.3.30's batched scan measured 128.20 Hz
at 0.75% per-grid duty.

| Level | Per-grid duty | Total CLEAR duty | ON | OFF |
|---|---:|---:|---:|---:|
| Low dim | 0.25% | 6.5% | 19,500 ns | 143,000 ns |
| Working dim | **0.5%** | **13%** | **39,000 ns** | **123,500 ns** |
| Medium, connected visual check | 0.75% | 19.5% | 58,500 ns | 104,000 ns |
| Brighter comparison | 1% | 26% | 78,000 ns | 84,500 ns |

Common settings, in ns:

```json
{
  "setup_ns": 500,
  "clock_low_ns": 1000,
  "clock_high_ns": 1000,
  "strobe_ns": 5000,
  "latch_guard_ns": 10000,
  "blank_before_ns": 10000,
  "blank_after_ns": 10000,
  "on_ns": 39000,
  "off_ns": 123500,
  "pulses": 1,
  "hold": 0,
  "slot": 0,
  "pair": 0,
  "alternate_ms": 0,
  "alternate_blank_steps": 0
}
```

Here transfer/guards consume 137,500 ns, so ON + OFF is only 162,500 ns.
Ignoring the transfer overhead gives the wrong brightness and frame rate.
The 0.5% setting is a working dim candidate, not an absolute luminance
calibration or a personal preference measurement.

Earlier 3.75% **total** scan duty meant only 0.144% per grid. Comparing
that with a 3.75% held character created a 26-fold duty mismatch.
Neither 3.75% nor the later experimental 30% total ceiling is a Toshiba
datasheet duty specification. The original captured Sony timing implies
about 3.69% per grid; this is a reference measurement, not permission to
increase an experimental driver's current.

## 5. Web operation

Open the ESP's current IP address on the trusted local network:

- `/`: internet radio, ICY song title, stream status and display-mode controls.
- `/screen`: full display editor, GPIO profile and timing laboratory.
- `/settings`: Wi-Fi settings.
- `/logs`: diagnostics.

The display editor retains both GPIO profiles and arbitrary timing inputs.
Changing a preset does not establish a safe physical connection.

For normal radio use:

1. Select AHCT v2 in `/screen` **while disconnected**, as described above.
2. Reconnect and enable the correctly isolated hardware safely.
3. Open `/`, choose a station and check its stream/decoder status.
4. Choose NOW PLAYING or Option 1 stereo spectrum and the tested per-grid
   brightness. Tick the hardware-ready authorization and press
   **Start / apply display**. Selecting a mode alone never starts output.
5. **Stop display / CLEAR LOW** blanks the screen without stopping radio.

NOW PLAYING uses 22 matrices. Text of 22 glyphs or fewer stays static;
longer text pauses initially for one second, then moves one character
every 300 ms, with a four-space cyclic gap. Metadata is followed on the
ESP; closing the browser does not stop updates. Repeated identical titles
do not restart the scroll.

Manual text/editor commands take ownership away from either radio mode.
Starting a new Timing request without `now_playing:true` also returns to
manual control. Stop never permits a later metadata update to restart output.

Radio starts NPO Radio 2 at boot, independently of display authorization.
Other presets are BNR, Radio 538 and SLAM; a custom direct HTTP/HTTPS MP3
or AAC URL can be saved. No playlist/HLS/webpage URL support is implied.
The Sony profile does not start AirPlay or drive the old mute, relay,
attenuator or PCM-I2S controls.

ICY `StreamTitle` is station-provided, can lead buffered audio, and is not
available on every stream. Missing/empty titles fall back to the station
name; connection failures show a reconnecting message instead of an old
song. Unsupported glyphs use the catalogue's fallback. The display bridge
preserves valid UTF-8, handles Latin-1 metadata and truncates long input
without splitting UTF-8 sequences.

### Option 1: stereo spectrum (0.3.29)

Select **Option 1 -- stereo spectrum, 11 bands per channel** on `/`, then
press **Start / apply display**. NOW PLAYING remains the default. Both modes
need the same hardware authorization; a page reload or selector change does
not start output. The webpage still shows the title while the Sony shows bars.

The left 11 matrices are LEFT-channel bands, the right 11 are RIGHT-channel
bands. Each half independently runs low-to-high frequency, left-to-right:
LEFT uses slots22..12; RIGHT uses slots11..1. Band edges, in Hz:

```text
20 | 37 | 70 | 132 | 247 | 462 | 866 | 1622 | 3040 | 5696 | 10673 | 20000
```

In 0.3.30, the fixed **left MONO + 1** legend marks the LEFT bank and the
separate **right MONO + 2** legend marks the RIGHT bank. These are bank labels,
not claims that the audio is mono. The left index 2 is kept off. They clear
when returning to NOW PLAYING or taking over with manual text. **STEREO still
follows actual decoded two-channel playback** and clears for mono/buffering;
it is never forced on by the spectrum. In text mode, the left MONO legend
continues to indicate an actual mono source.

The source is actual decoded stereo PCM **after resampling to 48 kHz**, not
ICY bitrate or an invented animation. Mono sources have already been duplicated
to both channels and therefore show matching bars. This is a visualization,
not an audio equalizer: it does not change frequency response or audio gain.

Analysis uses a 4096-sample periodic Hann window (85.3 ms, 11.72 Hz bin
spacing), packing the two channels into one complex FFT. Each band's summed,
window-normalized power maps from -60..0 dBFS onto zero to seven bottom-up
rows. A full-scale sine is approximately -3 dBFS under this RMS-power convention.
Attack is immediate; decay is 8 dB per completed analysis, targeting about ten
analyses per second. FFT work runs in a yielding low-priority core-0 task.
It is not performed in the high-priority audio writer or the display scan.
Sampled on-device analyses took 11.3-41.5 ms (median 16.6 ms), with updates
near the 10 Hz target and no dropped PCM frames during the checks.

The PCM tap never waits for space. Source-generation changes or dropped input
invalidate the accumulated window and clear bars rather than combining
discontinuous samples. Buffering silence decays to blank; loss of PCM delivery
for over 250 ms clears the accumulated window. Lazy initialization reserves
approximately 112 KiB PSRAM plus an 8 KiB task stack. After the first selection,
those allocations remain available; analysis/input copying are disabled in
text, manual and stopped modes.

All 26 scan slots, guarded timing and selected per-grid brightness are
unchanged. Zero amplitude is genuinely blank; row seven fills all 35 matrix
dots. Radio fixed indicators remain active and metadata updates cannot
overwrite the spectrum. Manual text, Stop and blank revoke spectrum ownership;
FFT updates cannot restart a stopped display.

Dense bars can illuminate more dots than text. Unchanged per-grid duty does
**not** mean unchanged aggregate current. Start dim and measure supply/current
before increasing brightness; this feature provides no electrical certification.

### Radio status indicators (0.3.28)

These update in either radio display mode, without a browser or a change to
scan duty.

| Indicator | Meaning |
|---|---|
| STEREO | Decoded source has two channels |
| Left MONO label | In text mode: actual mono source; in spectrum: MONO 1 labels LEFT |
| Right MONO + 2 | Spectrum mode only: labels RIGHT, not a mono-source claim |
| 14 signal bars | Station-reported ICY bitrate, scaled to 320 kbps |
| Bottom dots | Buffering, reconnecting, or bitrate unavailable; bars are off |
| TUNED | Decoding is active with valid source channels and no reported stream error |
| Left MUSIC / SPEECH | Recognized station genre; unknown/mixed genres leave both off |

For positive bitrate, bar count is `round(14 * kbps / 320)`, clamped to
1-14: 64 kbps gives 3 bars, 128 gives 6, 192 gives 8, 256 gives 11 and
320 or more gives 14. Bitrate is an encoding-rate proxy, **not** a
measurement of sound quality, Wi-Fi strength or network reliability.
Fixed-rate stations show steady bars; AAC and MP3 cannot be quality-ranked
solely by bitrate.

During buffering/reconnecting, STEREO, MONO, TUNED and the genre labels
clear; the bottom dots replace the bars. TUNED is a software playback
indication, not RF tuning or confirmation of a locked external S/PDIF receiver.
Channel labels describe the source, not the always-stereo S/PDIF container.

Genre comes from the stream's `icy-genre` header, exposed as
`now_playing.genre` and shown on the webpage. Pop/rock/Top 40 and recognized
music genres select MUSIC; talk/news/speech/sports select SPEECH. Conflicting
music-and-talk tags, Mixed and missing tags select neither. This is station
format metadata, not per-programme audio analysis or a guess from song titles.
In the checks, NPO Radio 2 reported Mixed, BNR Talk and 538/SLAM Top 40.

The source-status composer owns slot-0 mask `0x7FFF`, slot-24 mask `0x2013`
(left SPEECH/MUSIC/MONO and TUNED), and slot-25 STEREO bit `0x01`.
The radio display view additionally owns slot-24 mask `0x80C` for left indices
1/2 and the separate right MONO/2; combined slot-24 ownership is `0x281F`.
Spectrum sets left MONO/1 and right MONO/2 (`0x818`); text mode clears the
bank indices/right legend and restores source-based left MONO.
Other fixed fields, PROGRAM, MEMORY and red segments are preserved.
Manual control and Stop still take ownership away from the radio.

S/PDIF is 48 kHz stereo on GPIO42. This is a **3.3 V logic output**, not an
analogue speaker/headphone output. Use a compatible optical/transmitter
stage; audible Sony playback still requires a correctly connected audio
path. No Sony analogue route or audibility is established by decoder counters.

The HTTP controls have no authentication; do not expose this device through
Internet port forwarding.

## 6. Local HTTP API

`GET /api/screen` reports active pins, output state, requested/submitted
timing revisions, timing values, nominal cycle rate and `timing_grid_duty`
as a fraction. `now_playing` reports radio ownership; `text_width` is the
current text viewport width. GPIO levels are commanded states, not voltages.
`visualization` is `manual`, `now_playing` or `spectrum`. The `spectrum` object
reports `initialized`, `enabled`, completed `updates`, `dropped_frames`,
`last_fft_us`, `sample_rate`, 22 `levels` (LEFT then RIGHT), and `band_edges_hz`.
These software diagnostics do not replace analyser or audio-output checks.

Select a profile in a separate POST, stopped and physically disconnected:

```json
{"pin_profile":"ahct-v2","bench_confirmed":true}
```

Start a Timing scan using `sample:"Timing"`, the complete `timing` object
above and exactly 26 ten-digit hexadecimal `words`, indexed by logical
slot. Supply either a real disconnected-bench confirmation or explicit
live authorization. Add `"now_playing":true` to follow radio metadata;
this requires the normal full scan, not held/pair/alternation mode.
Add `"visualization":"spectrum"` with `"now_playing":true` to select Option 1;
use `"visualization":"now_playing"` to return to text. Omitting visualization
preserves the previous text behavior. Within Timing/Display commands, unknown
values, nonradio requests and held/pair/alternation combinations are rejected.
Switching modes on an existing
normal Timing scan does not reset the ESP or change the GPIO profile.

For a manual T, slot22 is `"010842109F"`; all other words can be
`"0000000000"`. Starting NOW PLAYING can use all-zero words; the bridge
supplies the current title/station.

Manual text on an already authorized running scan:

```json
{
  "sample":"Text",
  "text":"Artist - A longer song title that should scroll",
  "scroll":true,
  "width":22,
  "dim":true,
  "live_confirmed":true
}
```

Omitting `width` preserves the old nine-character API. In Timing mode,
actual brightness comes from ON/OFF and the scan schedule, not the
legacy `dim` boolean.

Stop with `{"sample":"stop"}`; poll until `running:false`. The separate
`blank` command requests immediate CLEAR blanking but is not a hardware
output-disable.

`GET /api/radio` reports station, connection/decode errors, frame counters
and the `now_playing` metadata object. POST `{"station":0}` selects NPO;
IDs 1/2/3 select BNR/538/SLAM. A custom stream is
`{"station":4,"url":"https://example.com/live.mp3"}`.

## 7. Toshiba limits and what has actually been checked

The [TD62C950RF datasheet](TD62C950RF.PDF) specifies recommended VDD
4.5-5.5 V, CK at most 8 MHz, CK HIGH/LOW at least 75 ns, and DATA
setup/hold at least 50 ns. STB pulse width is at least 70 ns and CK
rising to STB falling at least 50 ns. Input rise/fall must be at most
1 us. Absolute maximum ratings are not normal operating targets.

The recorded guarded settings have generous sampled timing margins.
Independent decoding models CK rising, STB HIGH transparency/LOW hold,
40-bit registers and grid CL overriding without resetting registers.
It checks the one-hot **connected** grid and expected word at every
visible enable; the board has 26 connected grid outputs and 14 NC.

Buffer-side captures cannot certify:

- actual voltage, overshoot, rise/fall or signal continuity at both TD chips;
- actual segment CL / both CHG states;
- high-voltage output settling at the real VFD load;
- source/sink current, dissipation, filament drive or long-term reliability;
- subjective flicker or calibrated luminance.

The Sony schematic annotates logic supplies around 5.7/5.8 V. Those
annotations are not present measurements and exceed the TD recommended
5.5 V maximum: measure the real supply before claiming full compliance.
Published output propagation/rise figures use stated test loads, not a
certificate for the Sony glass.

After the shared-STB correction, camera and complete 12 MS/s captures
verified held T, dim levels, nine-character text, ten digits, 22-position
mixed text, 22 nonblank dense glyphs and the legacy nine-character marquee.
Matched 0.5% held/scanned T camera readings differed about 4%; the dense
22-glyph reference differed about 6% from a single scanned T. One mixed
sample varied more. These are nonlinear camera readings, not a current
regulation measurement.

For simultaneous camera/analyser operation on the shared USB2 hub,
1080p15 camera plus 12 MS/s acquisition was reliable. 24 MS/s could silently
truncate despite sigrok returning success: always check actual duration.

### Installed radio integration: 0.3.26

With all six Sony signal wires physically disconnected, the installed image
decoded all four preset stations as MP3 and received live NPO song metadata.
Decoder errors, compressed-buffer underflows and S/PDIF write errors remained
zero during those checks. This verifies decoding/output counters, not audible
playback through the Sony.

A complete 7.5-second, 12 MS/s analyser acquisition checked the real radio
title at working dim while audio decoding continued. Fourteen complete
frame variants matched legitimate 22-character scroll windows or the
subsequent manual READY text. The independent TD model passed sampled
timing, one-hot connected grids, latch/blanking behavior and the conservative
2.4 us post-latch margin. The loaded scan measured about 127.38 Hz.
Repeated title polling did not restart scrolling; manual text released
radio ownership, and Stop remained stopped despite later metadata polling.
No additional S/PDIF underruns occurred during that acquisition; the counter
already contained one startup underrun.

The installed webpages were compared byte-for-byte with the source assets.
The radio-only profile also avoids querying the uninitialized AirPlay
receiver diagnostics: that inherited status call caused an interrupt
watchdog reset before being corrected. The display was left stopped,
AHCT v2 selected, with NPO Radio 2 running. A future reboot still requires
the disconnected profile-selection procedure.

### Indicator and coexistence checks: 0.3.28

Complete 5-second, 12 MS/s isolated captures verified NPO's 192 kbps/eight
bars/STEREO/TUNED, BNR's 128 kbps/six bars/SPEECH, 538's MUSIC, and the
buffering dots with the playback labels cleared. All passed the independent
TD model and guarded timing checks. Loaded refresh was about 126.7-126.9 Hz.
Mono behavior, field preservation and unchanged-title indicator updates
are covered by host tests; these live preset streams were all two-channel.

Longer coexistence tests exposed insufficient network-task stack space
and core-1 idle starvation. The network stack is now 12 KiB; observed
minimum reserve after repeated switches was 5,784 bytes. Identical timed
DMA waves are reused per completed buffer instead of regenerated every
refresh, retaining separate preload/continuation keys. Decoder bursts yield
periodically; the network session wake uses a semaphore separate from the
stream buffer's task notifications. Six station changes with a continuous
scan then completed without a panic or watchdog warning.

Separate steady 48 kHz and 44.1 kHz playback intervals with scanning showed
normal frame progress and no new DMA underruns, decoder errors, compressed
underflows or S/PDIF write errors. The DMA underrun counter can increase
during source resets; this is not a claim of gapless station switching.
CMake now declares `version.txt` as a configuration dependency; always
verify the reported image/runtime version after an incremental build.

### Stereo spectrum checks: 0.3.29

The application and matching radio page were installed with all six Sony
signals physically disconnected. Boot was verified stopped on the original
profile, with spectrum memory unallocated; AHCT v2 was selected while isolated.
The image is 3,130,736 bytes and fits the existing 3 MiB application partition
with 14,992 bytes spare. A verified 0.3.28 recovery image was retained locally.

A complete five-second, 12 MS/s music capture contained 33 legitimate changing
bar-frame variants. Every matrix word was one of the eight bottom-up levels,
fixed indicators remained valid, and each latch had 40 segment clocks and one
grid clock. The independent TD model passed one-hot connected grids,
latch/blanking behavior, sampled timing limits and the conservative 2.4 us
settling margin. Loaded refresh measured 126.57 Hz. Configured brightness
remained 0.5% per grid (approximately 0.494% after measured scan overhead).

Local 48 kHz MP3 fixtures were decoded through the actual radio/resampler/PCM
tap: a 996.09 Hz LEFT tone at 0.8 amplitude reached band 7/row 7, while an
8003.91 Hz RIGHT tone at 0.125 amplitude reached band 10/row 5. Swapping channels
swapped the peaks without cross-channel bars. A true mono source produced
identical halves (row 6 for a 0.5-amplitude tone); encoded silence blanked all
22 matrices. Band numbers here are one-based. These fixtures temporarily used
the custom station; the original settings were then restored and verified
byte-for-byte, including the originally empty custom URL.

Mode switching, unchanged metadata, manual ownership, invalid mode rejection
and Stop were checked on-device. FFT copying/analysis disabled in text/manual/
stopped modes, and later metadata/FFT work did not restart output. The monitored
runs had no panic/watchdog warning and no dropped PCM frames. Each steady
fixture interval and a separate 15-second music interval had no new decoder,
buffer, S/PDIF write or DMA-underrun errors; source changes still are not
guaranteed gapless. Host DSP/ownership/driver and webpage regressions also pass.
Sony optics, audible playback and physical voltage/current remain separate checks.

### Spectrum bank labels and scan-jitter correction: 0.3.30

The user confirmed the original spectrum looked good at 0.75% per grid,
then reported flicker on music. The earlier capture had 23 steady intervals
over 9 ms out of 399, with a 17.36 ms maximum instead of the usual 7.9 ms.
On Radio 538, content-only waveform patching alone still left a 38.79 ms
maximum. Frozen bars with FFT disabled also showed 20.23 ms gaps, ruling
out the animation alone as the explanation. The gaps were blanked intervals;
sampled active CLEAR pulses were not stretched.

The retained fix keeps the existing core assignments/priorities and batches
up to eight full scans per DMA block, with content-only SDATA patching. A
core-migration trial produced an audio underrun and was rejected. Three
complete five-second, 12 MS/s captures on NPO (48 kHz), Radio 538 and BNR
(44.1 kHz) passed the independent TD timing/one-hot/latch model, including
the new bank legends and source STEREO. After a 250 ms startup exclusion,
the worst frame intervals were 8.014, 8.021 and 8.016 ms respectively, with
**zero intervals over 9 ms**. Median refresh measured 128.20 Hz.

There were no new decoder, compressed-buffer, S/PDIF write or DMA-underrun
errors in those captures or a further 20-second steady Radio 538 interval;
FFT input drops remained zero. Sampled FFT cost was 11.6-27.8 ms, median
15.2 ms. Cold-start DMA count also stayed unchanged in the retained build.
Stop completed in 0.28 seconds and remained stopped. Bank-label cleanup on
text/manual takeover passed on-device; stereo, mono and buffering behavior
also have host coverage. The 28 host regressions and both browser checks pass.
The installed image is 3,131,408 bytes; the original settings and 0.3.29
recovery image are retained. The matching webpage offers the 0.75% preset.
This removes the measured software scan holes; a connected camera/user
recheck is still needed before claiming all perceived flicker is resolved.
Firmware 0.3.28 retains the same GPIO allocation and boot-stopped safety policy.

## 8. Troubleshooting

| Observation | First checks |
|---|---|
| Clean analyser, mixed/offset text | STB physically reaches BOTH chips; controller isolation; CL/CHG states |
| Completely blank | Sony logic powered, AHCT supply/common ground, each `/OE` LOW, correct profile, CLEAR polarity |
| Very faint scan versus held T | Compare **per-grid** duty, including all 26 slots and transfer/blank time |
| Ghosts only in averaged marquee images | Inspect individual frames; averages can combine different legitimate scroll positions |
| Missing title | Station's ICY metadata availability; stream status; station-name fallback |
| Title stops scrolling after refresh | Repeated title must not be re-parsed as a new song; use ESP-local NOW PLAYING |
| Display changes while editing | Manual commands must release radio ownership; inspect `now_playing` |
| Wrong pins after reboot | Boot selects original; disable outputs and isolate before selecting v2 again |
| Truncated analyser trace | Lower shared USB bandwidth; validate sample count and duration |

Always stop and hardware-disable before moving wires/probes. Do not use a
brighter setting to compensate for an unresolved wiring or latch fault.

## 9. Build, deployment and recovery

From `firmware/`:

```sh
pio run -e radio-s3
python3 -m unittest discover -s tests -p test_screen_replay.py -q
node tests/test_display_editor.js
node tests/test_radio_display.js
```

Run the C tests in `tests/test_icy_metadata.c` and
`tests/test_radio_display_text.c` with their corresponding source files.
The radio profile registers MP3/AAC, not unused audio/container codecs, to
fit the existing 3 MB application slots without changing the partition map.

Flash only after fresh physical-disconnect confirmation. Check active OTA
slot and existing partition layout, preserve NVS/Wi-Fi and keep a known-good
application backup. Application updates do not deploy HTML: update
`radio.html`, `screen.html`, `display-editor.js` and `display-reference.png`
in SPIFFS as needed, or install a matching filesystem image.

The existing local `/api/fs/upload?path=/spiffs/www/<asset>` endpoint accepts
raw asset contents. Use literal path slashes. Never put Wi-Fi credentials,
NVS dumps or personal station credentials in Git.

After flashing, verify version, webpage bytes, radio decoding/metadata,
stopped boot behavior and selected pins. Run an isolated analyser test
before reconnecting; a successful build or upload alone is not validation.
