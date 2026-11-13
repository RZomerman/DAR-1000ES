# DAR-1000ES Sound path

Internet-radio sound interface, and control logic

> **Measured update, 2026-11-04:** [Audio control findings and next-step plan](07-audio-control-findings-and-plan.md)
> documents ATT/SHIFT/LATCH/INIT captures. Startup includes two **16-clock**
> LATCH windows, not the previously hypothesized 12-clock transfer. The
> LSB-first interpretations are `0x442C` and `0x0400`; command meanings,
> voltage compatibility and working audio remain unverified. INIT and the
> other controls were on different ESP boards in that bench session.
> The GPIO tables below are design proposals, not the current display
> harness pinout. Use [chapter 7](07-audio-control-findings-and-plan.md) for
> audio evidence and [chapter 8](08-esp-display-driver-specification.md) for
> the independently confirmed display controls.

## Original audio architecture

The service documentation shows two distinct Sony devices in the digital audio path: IC501, a CXD2560M digital-filter device, and IC601, a CXD2562Q D/A converter. The intended reuse path is therefore: 

> Original digital tuner / decoder   
>         \|   
>         v   
> IC501  CXD2560M digital filter   
>         \|  left/right filtered serial data and clocks   
>         v   
> IC601  CXD2562Q D/A converter   
>         \|   
>         v   
> Original analogue filter / amplifier / output stage 

For the conversion, the ESP32 replaces the original digital-tuner audio source. The first-choice injection point is the PCM input of IC501. IC601 remains available as a documented fallback. 

**IC501**

IC501 receives a conventional serial audio set consisting of data, bit clock, word-select clock and master clock. This is the most attractive injection point because IC501 can continue to perform the original Sony filtering before passing audio to IC601. 

| **Signal**  | **Function**  | **Proposed ESP32 assignment**  | **Board connection**  |
|:--:|----|----|----|
| BCK1  | Serial bit clock  | GPIO16  | Reuse the IC501-side path through R501  |
| LRCK1  | Left/right word-select clock  | GPIO17  | Reuse the IC501-side path through R503  |
| DATA1  | Serial audio data  | GPIO18  | Reuse the IC501-side path through R502  |
| MCLK  | Master clock  | GPIO21  | Optional until the required clock relationship is verified  |

 

**Important  **The original source must not drive BCK1, LRCK1 or DATA1 at the same time as the ESP32. Inject on the IC501 side of the existing series resistors only after the source side has been isolated or the original source IC has been removed. 

 

**IC501 control interface **

IC501 also has ATT, SHIFT, LATCH and INIT control inputs. The related Sony CXD1244S documentation establishes the meaning of the three-wire control style: ATT is attenuator serial data, SHIFT is the shift clock and LATCH transfers the shifted word into the active register. INIT resynchronises the processing and establishes a defined start condition. 

| **Proposed ESP32 GPIO**  | **IC501 signal**  | **Purpose**                           |
|:---------------:|-------------------|---------------------------------------|
|     GPIO10      | ATT               | Serial attenuation/control data       |
|     GPIO11      | SHIFT             | Shift-clock input                     |
|     GPIO12      | LATCH             | Loads the shifted control word        |
|     GPIO13      | INIT              | Initialisation and resynchronisation  |

 

The related CXD1244S documentation originally motivated a 12-bit LSB-first
attenuation hypothesis, with 0x400 representing 0 dB and 0x000 silence.
That is not a validated CXD2560M command specification. The measured Sony
startup has two 16-clock transfers with LATCH HIGH during shifting and falling
afterward. Rising-edge data interpreted LSB-first gives `0x442C` then `0x0400`.
The latter matches the candidate value, but no attenuation response has been
confirmed. INIT rises earlier in startup and remains HIGH. Use the measured
findings and verification gates in the linked plan, not the old 12-bit replay
proposal or a guessed INIT pulse sequence.

**Fallback injection point: IC601 **

The CXD2562Q pin information provides a second, documented injection route that bypasses IC501. IC601 accepts separate left- and right-channel serial inputs plus BCK and LRCK. The device is strapped for a 20-bit input word in this receiver. 

| **IC601 pin**  | **Name**  | **Function**  | **Use in fallback design**  |
|:--:|----|----|----|
| 36  | LRCKI  | Left/right clock input  | Drive from ESP32 word select  |
| 37  | DRI  | Right-channel data input  | Requires a separate right-channel serial stream  |
| 38  | DLI  | Left-channel data input  | Requires a separate left-channel serial stream  |
| 39  | BCKI  | Bit-clock input  | Drive from ESP32 clock output  |
| 33  | INIT  | Synchronisation input  | Pulse after clocks are stable  |
| 34  | MUTER  | Right-channel mute, high = mute  | Keep low for normal output  |
| 35  | MUTEL  | Left-channel mute, high = mute  | Keep low for normal output  |

 

**Design consequence  **IC601 does not present the usual single stereo DATA input. It has independent DLI and DRI inputs. Direct drive is therefore possible in principle but requires confirmation that the ESP32 peripheral or supporting firmware can generate the exact dual-data timing expected by IC601. This fallback is less convenient than feeding IC501. 

 

**Logic levels and protection **

The Sony devices are described in a 5 V logic environment, while the ESP32 uses
3.3 V GPIO. Measure the actual supplies and verify exact input thresholds
before connecting active outputs. Do not assume direct 3.3 V drive will work
or be safe under all power states; use a verified interface and isolate the
original source first. A failed response may be an electrical-level problem
rather than a protocol problem.

- For initial bench testing, reuse existing Sony series resistors where they are already present. 

<!-- -->

- R501, R502 and R503 should remain in circuit on the BCK1, DATA1 and LRCK1 paths. 

<!-- -->

- Never connect a Sony 5 V output directly into an ESP32 input without level reduction. 

<!-- -->

- For the final design, use a unidirectional 3.3 V to 5 V buffer from the 74AHCT or 74HCT family. Avoid common BSS138 bidirectional I2C level-shifter modules on BCK, LRCK or MCLK because those modules are not the preferred choice for clean clock edges. 

**Complete ESP32 allocation **

This is the original single-ESP proposal, not the current verified allocation.
INIT was on a different ESP from ATT/SHIFT/LATCH in the recorded audio bench
session. Reconcile actual board assignments and startup coordination before
implementing this table; do not combine it with another profile's GPIO map.

|     **Function**       | **Signal**      | **GPIO**  |
|:----------------------:|-----------------|-----------|
|   Grid driver IC702    | FLGDATA         | 4         |
|   Grid driver IC702    | FLGCLK          | 5         |
|   Grid driver IC702    | FLGCLEAR        | 6         |
| Segment driver IC703   | FLSDATA         | 7         |
| Segment driver IC703   | FLSCLK          | 8         |
| Shared display strobe  | FLSTB           | 9         |
|     IC501 control      | ATT             | 10        |
|     IC501 control      | SHIFT           | 11        |
|     IC501 control      | LATCH           | 12        |
|     IC501 control      | INIT            | 13        |
|     Digital audio      | BCK             | 16        |
|     Digital audio      | LRCK            | 17        |
|     Digital audio      | DATA OUT        | 18        |
|     Digital audio      | MCLK, optional  | 21        |
|    TCA8418 keypad      | SDA             | 1         |
|    TCA8418 keypad      | SCL             | 2         |

 

**Bench validation sequence **

1.  Confirm a common ground between the ESP32 and the Sony digital board. 

<!-- -->

2.  Identify and isolate the original source side of R501, R502 and R503. Do not allow two active outputs to share a line. 

<!-- -->

3.  Keep R501, R502 and R503 between the ESP32 wiring and IC501. 

<!-- -->

4.  Power the Sony unit and verify the supply rails around IC501 and IC601 before attaching ESP32 signals. 

<!-- -->

5.  Start MCLK, BCK and LRCK, then present a known low-level PCM test tone on DATA. 

<!-- -->

6.  Establish the datasheet-supported INIT sequence and safe mute state; the captured power-up rise alone does not specify a replay sequence.

<!-- -->

7.  Validate the measured 16-clock transfer framing on an unconnected ESP first. Do not treat 0x0400 as confirmed full scale or send guessed 12-bit commands.

<!-- -->

8.  Observe MUTEL and MUTER and check audio progressively at the IC601 outputs and then through the analogue stage. 

<!-- -->

9.  If IC501 does not respond, check the verified logic levels, clock/PCM format, INIT and mute states before rejecting the protocol hypothesis.

**Known facts, deductions and open questions **

| **Status**  | **Statement**  |
|----|----|
| Confirmed  | IC501 is the CXD2560M digital-filter stage and IC601 is the CXD2562Q D/A-converter stage in the receiver.  |
| Confirmed  | IC601 accepts LRCKI, BCKI, DLI and DRI and provides independent left/right mute inputs.  |
| Proposed  | The original single-ESP GPIO allocation reserves GPIO10 to GPIO13 for IC501 control and GPIO16, GPIO17, GPIO18 and GPIO21 for digital audio; reconcile with the actual split-board wiring.  |
| Strong deduction  | ATT, SHIFT and LATCH on IC501 form a serial attenuation/control interface similar to the documented CXD1244S implementation.  |
| Measured  | Startup contains two 16-clock LATCH HIGH windows; rising-edge ATT data interpreted LSB-first gives 0x442C and 0x0400. Command meanings and specified sampling edge remain unverified. |
| Unverified  | The CXD2560M reliably recognises 3.3 V ESP32 outputs as logic high.  |
| Unverified  | The exact PCM format, sample rate, bit alignment and required MCLK ratio accepted by IC501 in this receiver.  |

 

**Recommended direction  **Prototype through IC501 first. It preserves the original Sony filter and DAC path and needs only the standard serial-audio stream plus four slow control lines. Keep the IC601 pins accessible as test points and as a fallback, not as the primary design.
