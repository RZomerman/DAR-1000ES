# Live ESP32 Display Driver and Validation

This chapter records the historical 2026-11-09 prototype and its first live
validation on a Sony DAR-1000ES, followed by the current status below.
It complements the capture and optical mapping evidence in
[Chapter 6](06-direct-display-driving.md) and the consolidated design notes in
[Chapter 8](08-esp-display-driver-specification.md).

## Current status: 2026-11-13

Firmware 0.3.25 subsequently produced clean 26-slot text across all 22
matrices after STB was connected to **both** TD chips. Firmware 0.3.26
adds radio and authorized, full-width scrolling NOW PLAYING. Use the
[screen-driver operating guide](13-screen-driver-operating-guide.md)
for current AHCT v2 wiring, per-grid brightness, operation and the later
camera/analyser results. Voltage/current certification remains outstanding.

The later firmware 0.3.23 held-grid tests used GPIO preload and CLEAR-only
DMA to map all functional elements in the FL701 reference. Individual
controls and matrix orientation were confirmed; full-scan corruption was
still unresolved at that stage. Use the
[fixed-element](08-esp-display-driver-specification.md#6-fixed-element-control-guide)
and [matrix](08-esp-display-driver-specification.md#7-generic-5x7-matrix-guide)
guides for current assignments, not tentative interpretations of the early
MEMORY scan below.

The camera inventory used 3.75% held-grid duty (37.5 us ON / 962.5 us OFF);
the existing guard was not raised. This is controlled optical evidence,
not electrical/current-rating certification. The GPIO table and 250 Hz
measurements below belong specifically to the older 0.3.14 experiment,
not current wiring or a validated production scan.

The installed translator is kept enabled. Before any USB reset or reflash,
confirm all six Sony display signal wires are physically disconnected.
Switching Sony off alone is not electrical isolation.

## What is being driven

The display uses two Toshiba TD62C950RF serial drivers: IC702 selects the
display grid and IC703 drives the segments. The ESP32 prototype drives their
serial display interface directly; this is separate from the parallel
controller bus documented in [Chapter 5](05-bus-capture-findings.md).

The captured Sony scan has 26 slots per frame. Each slot shifts 40 segment
bits and one grid bit. The original scan is approximately 301 us per slot,
or 127.8 Hz per frame. The 0.3.14 experimental firmware preserved that
slot structure but deliberately shortened the timing to test whether a faster
refresh reduces visible flicker.

## Historical 0.3.14 hardware and signal map

The tested controller is an ESP32-S3, firmware 0.3.14, using a TXS0108E
translator between 3.3 V ESP GPIO and the 5 V display logic. The grounds are
common. The user measured 5 V on the translator outputs. Keep the probes on
the low-voltage logic signals; the VFD itself uses hazardous high voltage.
At 5 V driver supply, the TD62C950RF datasheet requires at least 3.5 V for a
logic HIGH, so direct 3.3 V ESP output is not guaranteed to meet the input
specification.

| Sony signal | Function | ESP32-S3 GPIO | Saleae channel |
|---|---|---:|---|
| FLGDATA | Grid serial data | 5 | D0 |
| FLGCLK | Grid shift clock | 11 | D1 |
| FLCLEAR | Grid blanking | 7 | D2 |
| FLSTB | Shared latch control | 8 | D3 |
| FLSDATA | Segment serial data | 9 | D4 |
| FLSCLK | Segment shift clock | 10 | D5 |
| Translator OE | TXS0108E output enable | 4 | D6 |
| Unassigned | — | — | D7 |

This GPIO map is specific to firmware 0.3.14; it must not be treated as the
current live wiring. Keep GPIO12-14
disconnected from the Sony; they serve as unused/control lanes for the i80
peripheral.

TXS0108E OE is held **HIGH** and has an external 10 kOhm pull-up to 3.3 V
(VCCA). VCCA is 3.3 V and VCCB is 5 V. Do not pull OE up to 5 V or command it
LOW; the installed setup is intentionally kept enabled. Firmware returns the
six display signals to DATA LOW, CLEAR LOW, STB LOW, and both clocks HIGH
after a scan. These are commanded logic levels, not a hardware blanking
fail-safe.

## Historical 0.3.14 driver implementation

The screen-only firmware builds each scan from captured signal timing and
streams it with the ESP-IDF LCD i80 peripheral at an 8 MHz sample clock using
DMA. That is the waveform sample clock, not the TD62C950RF shift-clock rate.
The continuous dim scan resamples every slot to 1,230 samples:

| Parameter | Current test setting |
|---|---:|
| Peripheral/DMA sample rate | 8 MHz |
| Samples per slot | 1,230 |
| Slot duration | 153.75 us |
| Slots per frame | 26 |
| Frame duration / refresh | About 4.00 ms / 250 Hz |
| Scaled dim CLEAR-low interval | About 103 us per slot |
| DMA waveform block | 8 consecutive frames |

The dim interval is scaled from the roughly 201 us measured in the original
Sony dim waveform. This is a timing experiment, not a reproduction of the
original 301 us slot timing. The initial DMA transfer includes register
preload; continuation transfers avoid repeating the long blank/preload
prefix that previously caused visible gaps at block boundaries.

The MEMORY test sends a 26-word frame with slot 25 set to `0x0000000002`,
the captured MEMORY mask. The remaining words are zero. The display must be
scanned continuously; this word is not a standalone command.

The firmware implementation and its tests are not yet published in this
repository; this chapter documents the tested configuration and observed
results independently of those source files.

## Live validation

Two five-second dim MEMORY runs were made with the Sony connected and a
24 MS/s Saleae capture. The repeat capture measured **249.7 Hz** (4.004 ms
mean frame period; 3.997-4.112 ms observed). The MEMORY data remained stable
at its grid phase. OE stayed HIGH throughout; the controller stopped cleanly
with `last_error: 0` and restored the expected idle pin levels.

The display lit MEMORY, confirming that the direct scan can reach the
intended indicator. The user's visual assessment was that it remained
mostly flickery, with another element faintly illuminated. A 30 fps webcam
recording also showed MEMORY lit and a second faint point, but it cannot
reliably diagnose flicker at a 250 Hz refresh rate or identify that point.
Visual stability and the identity of the faint element therefore remain
unconfirmed.

## Limits of the 2026-11-09 tests

- The 250 Hz scan is intentionally faster than the captured Sony baseline;
  success at this timing does not establish protocol-faithful operation.
- The analyser confirms scan cadence and stable serial data, not the visual
  quality of the VFD or the cause of the remaining flicker.
- Do not infer the meaning of the faint element from the webcam image alone.
- Keep OE HIGH. Before any USB reset or reflash, isolate the powered Sony
  from the controller. Never connect ESP GPIO directly to the 5 V driver
  inputs.
- A higher-frame-rate, short-exposure recording or a photodiode measurement
  would be needed to distinguish real scan flicker from camera aliasing.

Those tests established an analyser-verified experimental scan that
lights the selected MEMORY indicator, but it is not yet a verified stable
replacement for the original Sony display driver.
