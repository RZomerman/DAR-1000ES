# Sony DAR-1000ES Conversion Project

This repository contains the project documentation converted from the original Word document into chapter-based GitHub Flavored Markdown.

## Chapters

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

Images used by the chapters are stored in [`media/`](media/).

## Control Integration Options

1. **Reuse the original controls (partially characterized):** Investigate the parallel bus between the system controller and display/front-panel controller IC701 before bypassing the original control firmware. See the [internal bus protocol specification](SONY_DAR1000ES_Protocol_Spec.md) and [2026-11-02 capture findings](05-bus-capture-findings.md). Passive captures establish repeatable framing and key/display correlations, but masked bits, driver ownership and electrical assumptions still block reliable emulation.
2. **Direct display-driver control (fallback, passively decoded):** Captures at IC702/IC703 establish a 26-slot scan, 40 segment clocks and one grid clock per slot, readable text and isolated annunciator bits. See [Direct Display driving](06-direct-display-driving.md) for measured timing, wiring, glyphs and mappings. Active driving and the electrical interface remain unverified; [DAR-1000ES Display Control](02-display-control.md) retains the original hardware proposal.
