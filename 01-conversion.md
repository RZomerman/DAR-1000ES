# DAR-1000ES Conversion

The Sony DAR-1000ES is an old relic that used to be a satellite tuner. Its front panel has a number of buttons that we can reuse, its output is digital and internally it even has its own DAC (Digital Analog Conversion) chip.

<img src="media/image1.jpeg" alt="The image shows a SONY audio equipment with various settings and controls, including a display showing 118.0MHz, a linear converter, and a fine-tuning dial. AI-generated content may be incorrect." />

## Design objective 
The conversion should retain as much of the original Sony audio chain as practical. The preferred design therefore keeps the existing digital-filter, D/A-converter and analogue output stages, and replaces only the obsolete programme source with an ESP32-generated digital audio stream.

The idea is to reuse the original Sony digital-filter and DAC stages where possible and feeding the digital audio from the ESP32 rather than adding an external DAC. This should preserve the original analogue filtering, muting and output circuitry (and therefore sound profile).

Given the front panel buttons (with items like NEWS, AFFAIRS, INFO, SPORT, etc..) this unit is actually pretty good to become a streamer. [Piotr D. on YouTube](https://youtu.be/dSmdoRHq8-w?si=nIIm9IDE0pOxPFWs) for example managed to convert it already. However, for his conversion he replaced the entire display, used his own DA conversion and picked up at the analog stage of the old unit (from what I can see in the pictures). In my conversion I will be attempting to use as much as possible from the original unit, including the DAC, the analog amplifiers, the display, the buttons and powersupply.

The main idea:

- Reuse as much as possible

- Implement an ESP32 to drive the display and button inputs

- Build an application that can stream internet radio stations

- Output to I2S (digital) sound directly into the DAC
