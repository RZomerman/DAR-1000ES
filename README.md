# Sony DAR-1000ES Conversion Project

This repository contains the project documentation converted from the original Word document into chapter-based GitHub Flavored Markdown.

## Chapters

For current AHCT wiring, brightness calculations and radio operation, start
with the [screen-driver operating guide](13-screen-driver-operating-guide.md).
Shared STB on **both** TD chips corrected the earlier multiplexing corruption;
clean 22-matrix text and scrolling were observed. Firmware 0.3.28 includes
internet radio, explicitly authorized full-width NOW PLAYING, and source
channel/bitrate/playback/genre indicators.
Installed 0.3.29 adds selectable Option 1 stereo spectrum (11 real PCM frequency
bands per channel), verified with isolated TD-model captures and decoded
stereo/mono/silence fixtures; the user confirmed the bars at 0.75% brightness.
Version 0.3.30 adds MONO 1/2 bank labels while retaining actual-source STEREO
and fixes measured scan gaps with longer DMA batching. A connected camera
snapshot confirms all 22 bars and STEREO/MONO 1/MONO 2/TUNED at 0.75%
per-grid duty. User confirmation of residual flicker is still pending;
a still image cannot establish temporal stability.
Physical voltage/current validation remains outstanding.
Firmware 0.3.31 boots with the fixed AHCT v2 GPIO map. Display output still
starts stopped; the legacy profile remains an isolated-only runtime option.
Installed firmware 0.3.32 adds eight display modes and shared short-/long-press
controls for a future Display Mode button, with 0.25% / 0.5% / 0.75% DIM cycling.
All eight modes and DIM cycling were checked on the isolated ESP; after
reconnection, Equalizer was restored at 0.75% per-grid duty.

For current display control, start with the
[fixed-element guide](08-esp-display-driver-specification.md#6-fixed-element-control-guide)
and [generic 5x7 matrix guide](08-esp-display-driver-specification.md#7-generic-5x7-matrix-guide).
The [annotated reference comparison](media/display-reference-coverage.svg)
matches every functional symbol in the FL701 drawing to an observed command.
Element mapping is complete at that level. Clean full-screen multiplexing
has now been observed with shared STB; electrical validation remains outstanding.

The [complete display bitmap catalogue](10-display-bitmap-catalogue.md)
includes 5x7 pixel patterns and HEX values for uppercase/lowercase letters,
digits, punctuation, a music note and seven level bars.

1. [DAR-1000ES Conversion](01-conversion.md)
2. [DAR-1000ES Display Control](02-display-control.md)
3. [DAR-1000ES Sound Path](03-sound-path.md)
4. [DAR-1000ES Program and Keypad Baseline](04-program-and-keypad-baseline.md)
5. [Bus Capture Findings and Key Reports](05-bus-capture-findings.md)
6. [Direct Display driving](06-direct-display-driving.md)
7. [Audio Control Findings and Next-Step Plan](07-audio-control-findings-and-plan.md)
8. [ESP32 Display-Driver Specification](08-esp-display-driver-specification.md)
9. [Music Note Bitmap](09-music-note-bitmap.md)
10. [Display Bitmap Catalogue](10-display-bitmap-catalogue.md)
11. [Live ESP32 Display Driver and Validation](11-live-display-driver-and-validation.md)
12. [TD62C950RF Timing Revalidation](12-td62c950rf-timing-revalidation.md)
13. [Screen Driver Operating Guide](13-screen-driver-operating-guide.md)

Images used by the chapters are stored in [`media/`](media/).

The [ESP32 firmware](firmware/README-DAR-1000ES.md) includes internet radio,
the opt-in display timing/NOW PLAYING profile and the preserved upstream license.

## Control Integration Options

1. **Reuse the original controls (partially characterized):** Investigate the parallel bus between the system controller and display/front-panel controller IC701 before bypassing the original control firmware. See the [internal bus protocol specification](SONY_DAR1000ES_Protocol_Spec.md) and [2026-11-02 capture findings](05-bus-capture-findings.md). Passive captures establish repeatable framing and key/display correlations, but masked bits, driver ownership and electrical assumptions still block reliable emulation.
2. **Direct display-driver control (optically mapped, driver experimental):** Captures at IC702/IC703 establish a 26-slot scan, 40 segment clocks and one grid clock per slot. Controlled one-hot tests now map all 770 matrix dots and all fixed functional elements shown in the reference. See the [operating guide](13-screen-driver-operating-guide.md), [element guide](08-esp-display-driver-specification.md) and [capture/validation history](06-direct-display-driving.md). Shared STB on both TD chips now gives clean full-width text and scrolling; electrical margins and current remain unverified. [DAR-1000ES Display Control](02-display-control.md) retains the original hardware proposal, not current wiring instructions.
