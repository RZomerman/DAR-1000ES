# Sony DAR-1000ES Conversion Project

This repository contains the project documentation converted from the original Word document into chapter-based GitHub Flavored Markdown.

## Chapters

1. [DAR-1000ES Conversion](01-conversion.md)
2. [DAR-1000ES Display Control](02-display-control.md)
3. [DAR-1000ES Sound Path](03-sound-path.md)
4. [DAR-1000ES Program and Keypad Baseline](04-program-and-keypad-baseline.md)

Images used by the chapters are stored in [`media/`](media/).

## Control Integration Options

1. **Reuse the original controls (unvalidated):** Investigate the parallel bus between the system controller and display/front-panel controller IC701 before bypassing the original control firmware. See the [internal bus protocol proposal](SONY_DAR1000ES_Protocol_Spec.md). Signal meanings, timing, electrical assumptions, and command formats still require validation; this is not a proven protocol specification.
2. **Direct display-driver control (fallback):** If Option 1 is impractical, investigate driving the two separate display-driver chips, IC702 and IC703, as described in [DAR-1000ES Display Control](02-display-control.md).
