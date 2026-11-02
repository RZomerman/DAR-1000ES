# Sony DAR-1000ES Conversion Project

This repository contains the project documentation converted from the original Word document into chapter-based GitHub Flavored Markdown.

## Chapters

1. [DAR-1000ES Conversion](01-conversion.md)
2. [DAR-1000ES Display Control](02-display-control.md)
3. [DAR-1000ES Sound Path](03-sound-path.md)
4. [DAR-1000ES Program and Keypad Baseline](04-program-and-keypad-baseline.md)
5. [Bus Capture Findings and Key Reports](05-bus-capture-findings.md)

Images used by the chapters are stored in [`media/`](media/).

## Control Integration Options

1. **Reuse the original controls (partially characterized):** Investigate the parallel bus between the system controller and display/front-panel controller IC701 before bypassing the original control firmware. See the [internal bus protocol specification](SONY_DAR1000ES_Protocol_Spec.md) and [2026-11-02 capture findings](05-bus-capture-findings.md). Passive captures establish repeatable framing and key/display correlations, but masked bits, driver ownership and electrical assumptions still block reliable emulation.
2. **Direct display-driver control (fallback):** If Option 1 is impractical, investigate driving the two separate display-driver chips, IC702 and IC703, as described in [DAR-1000ES Display Control](02-display-control.md).
