# Audio control findings and next-step plan

Bench session: 2026-11-04. **Passive captures only; no ESP-driven audio or
confirmed attenuation response yet.**

## Scope and constraints

The Sony currently displays **No DSR**. There is no working decoded programme
audio with which to test attenuation changes. This does not prevent observing
startup control signals, but it prevents confirming command meanings through
their effect on original programme audio.

The audio path is IC501 (CXD2560M digital filter) -> IC601 (CXD2562Q DAC) ->
original analogue output stage. Feeding the PCM input of IC501 remains the
preferred conversion route; bypassing IC501 is a fallback, not the next step.
See [Sound path](03-sound-path.md) for the architecture.

Only one analyser is connected. It has been moved from the display drivers to
the audio controls, so these captures have **no simultaneous display data**.
INIT is assigned to a different ESP board from ATT/SHIFT/LATCH in the current
hardware arrangement. The older single-ESP GPIO table is therefore a proposal,
not a verified description of the current wiring.

## Capture wiring and method

| Physical channel | Software/raw bit | Signal | User-confirmed board point |
|------------------|------------------|--------|----------------------------|
| CH1 | D0 / bit 0 | ATT | R504 |
| CH2 | D1 / bit 1 | SHIFT | R505 |
| CH3 | D2 / bit 2 | LATCH | R506 |
| CH4 | D3 / bit 3 | INIT | INIT connection; exact board point not recorded |

These are probe-point identifications, not independently established resistor
side or IC-pin continuity measurements. Maintain common logic ground.
The analyser sampled at **8 MHz / 125 ns** and stored one raw byte per sample.
The final four-channel startup capture is 30 seconds / 240 MB.

```sh
sigrok-cli --scan
sigrok-cli --driver fx2lafw --config samplerate=8m \
  --samples 240000000 --output-format binary \
  --output-file sony-att-shift-latch-init-startup.bin
```

The command assumes only one fx2lafw analyser is connected. Record before
switching on, then leave probes and controls untouched.

Earlier acquisitions had missing SHIFT/LATCH activity while contacts were
being improved. They are not evidence that the actual controller omitted
those signals. Subsequent clean startups reproduce the same serial bitstrings.

## Measured four-channel startup

Times below are seconds from the start of
`sony-att-shift-latch-init-startup.bin`, not from a measured supply threshold.
Before power-on, all four sampled inputs were LOW; unpowered logic readings
must not be treated as valid powered idle levels.

| Time | Observation |
|------|-------------|
| 4.415377 | SHIFT rises as the unit powers up; not counted as a command |
| 4.415656 | INIT rises and remains HIGH for the rest of the capture |
| 4.472885-4.541069 | LATCH is HIGH for about 68.184 ms, without a decoded SHIFT burst |
| 4.575348-4.576099 | 96 SHIFT rising edges; LATCH remains LOW |
| 5.224486-5.224568 | First short LATCH HIGH window, containing 16 SHIFT rising edges |
| 5.224600-5.224682 | Second short LATCH HIGH window, containing 16 SHIFT rising edges |
| 5.272306 onward | One initial 24-clock burst, then repeated alternating 24-clock patterns; no further LATCH transitions |

The first 96-clock burst starts about **159.692 ms after INIT rises**.
The first short LATCH window begins about **808.830 ms after INIT rises**.
These are observed delays in one recording, not established minimum timing
requirements. INIT's active polarity, destination(s), relation to stable audio
clocks and warm-reset behavior remain unverified. A single power-up rise does
not demonstrate a deliberate reset pulse.

### Two latched 16-clock transfers

An initial decoder grouped clock edges separated by less than 1 ms. That made
these two transfers look like one 32-clock group. Separating them using LATCH
reveals **two 16-clock windows**.

ATT bits below are sampled at SHIFT rising edges, in capture order. Hex values
are given in both interpretations so the bit-order assumption stays explicit.

| Transfer | ATT bitstring, first bit on the left | MSB-first interpretation | LSB-first interpretation |
|----------|--------------------------------------|--------------------------|--------------------------|
| First | `0011010000100010` | `0x3422` | `0x442C` |
| Second | `0000000000100000` | `0x0020` | `0x0400` |

LATCH is HIGH while each of these words shifts and falls after the final
clock. Its HIGH duration is about **82.1-82.3 us**, with about **31.75 us**
LOW between windows. The first clock follows LATCH rising by about 10.5 us;
the last clock precedes LATCH falling by about 7.4 us. The 16 clocks span
about 64 us (roughly 235 kHz average across 15 intervals).

This establishes observed framing, not the IC's specified sampling/latch edge.
Data can change near SHIFT falling edges; rising-edge decoding is more stable
in the repeated captures. Timing and actual input edge still need verification.

The second word's LSB-first value **0x0400** matches the candidate full-scale
value from the related CXD1244S documentation. That correlation does **not**
prove CXD2560M command semantics, register addressing or attenuation response.
The first word's meaning is unknown.

**The earlier 12-clock replay proposal is superseded:** the measured latched
windows contain 16 clocks. Do not send a guessed 12-bit attenuation command.

### Unlatched clock/data traffic

The repeated 96-clock startup bitstring is:

```text
001000101011000000000000000010000000000000000000000000011001101101000000000001111011011011110000
```

The first later 24-clock pattern is:

```text
001000101011000000000000
```

The two recurring 24-clock patterns are:

```text
000001000011011011110000
000001111011011011110000
```

In the final recording there are **189 complete 24-clock bursts**: one initial
pattern, then 94 of each recurring pattern. LATCH stays LOW during them.
They must not be interpreted as latched attenuation updates.

Shared clock/data wiring to another device is one possible explanation;
incorrect/incomplete net identification or another interface behavior remains
possible. **No DSR may explain unchanged audio settings, but does not by itself
explain the destination or meaning of this traffic.** Do not replay all captured
traffic indiscriminately.

## Evidence and limits

| Capture prefix | Principal result |
|----------------|------------------|
| `sony-att-shift-latch-startup` | Initial 60-second attempt; SHIFT/LATCH not acquired |
| `sony-att-shift-latch-recheck2` | 96-clock burst observed after contact changes |
| `sony-att-shift-latch-clean-startup` | 60 seconds; matching 96/32-clock groups and 417 later 24-clock bursts |
| `sony-att-shift-latch-clean-startup2` | 30 seconds; identical startup bitstrings and 175 later 24-clock bursts |
| `sony-att-shift-improved-startup` | Improved SHIFT contact; identical startup bitstrings, 159 complete later 24-clock bursts and a final capture-truncated burst |
| `sony-att-shift-latch-init-startup` | 30 seconds; INIT included, two 16-clock LATCH windows resolved |

Raw `.bin`, decoded JSON and analysis scripts are retained locally, not
committed here. No PCM clocks/data, supply voltages, analogue output or mute
signals were captured simultaneously. These recordings cannot establish a
safe electrical interface, accepted PCM format or a working audio path.

## Next steps under the current constraints

### 1. Preserve the working hardware; finish the desk work

Keep the original controllers connected; no trace cutting or active ESP drive.
Use the schematic and the exact CXD2560M/CXD2562Q datasheets to trace
R504/R505/R506, INIT and any shared data/clock branches. Establish which sides
of the resistors are source and receiver, command format, input thresholds,
clocking and mute behavior. Cross-check the TD display-driver requirements
separately using [Direct Display driving](06-direct-display-driving.md).

**Exit condition:** a reviewed net map and explicit list of electrical/timing
requirements. Unknown command meanings stay marked unknown.

### 2. Make passive measurements that do not require a DSR programme

Measure logic supply and signal HIGH levels with suitable instruments; the
logic analyser cannot measure voltage or certify signal integrity.
If another capture is needed, target a concrete question rather than repeating
identical startups: inspect audio BCK/LRCK/DATA and any existing master clock,
or correlate the two LATCH windows with INIT and mute states.

One 8-channel analyser can keep ATT/SHIFT/LATCH/INIT on CH1-4 and use CH5-8 for
four selected audio clocks/data signals, provided their voltage levels and
the analyser's bandwidth are suitable. An 8 MHz recording cannot reliably
characterize fast MCLK; use a suitable scope or faster acquisition equipment.
No DSR may leave some clocks/data absent or idle, so absence does not prove a
format. Do not guess standard I2S merely from signal names.

**Exit condition:** enough clock/format evidence to configure a source, or a
clearly documented dependency on a datasheet or better measurement equipment.

### 3. Resolve the two-ESP startup arrangement before firmware drives anything

Keep ATT/SHIFT/LATCH together on their ESP. Moving INIT to that same ESP would
simplify ownership, but is not mandatory if the existing split is retained.
For a split design, define one startup coordinator and an explicit ready/
handshake mechanism; do not rely on two independent fixed boot delays.
Both boards need common ground and electrically safe interfaces.

On either ESP reboot or communication loss, prevent unexpected INIT changes
or partial commands and hold the audio path in a verified safe/muted state.
The exact INIT default cannot be selected until its polarity/function is
known. Neither board may drive an unpowered receiver through its inputs.

**Exit condition:** assigned signal ownership and a defined power-up, reset,
fault and shutdown state machine. Existing proposed GPIO tables must be
reconciled with the actual board assignments before implementation.

### 4. Build firmware and test its outputs without connecting them to Sony

Implement a configurable 16-bit control transfer capable of reproducing the
two measured bitstrings and LATCH windows. Treat these as captured patterns,
not proven volume commands. Compare ESP-generated waveforms with the analyser
while disconnected from the Sony inputs.

Prepare a low-level PCM test source only after the required clock/format is
known. Keep this audio task separate from display scanning so one can be
validated without destabilizing the other. The display prototype can likewise
be checked on the analyser using 26-slot test frames, with verified blanking
and fault behavior before any VFD connection.

**Exit condition:** repeatable standalone waveforms, safe boot/reset behavior,
and no uncontrolled output pulses.

### 5. Install a reversible, level-correct interface

After confirming voltage requirements, select suitable unidirectional buffers
(for example, 5 V-powered AHCT if its specifications match the measured
interface), output-enable defaults and power sequencing. Do not assume Sony
recognizes ESP 3.3 V HIGH, and never feed Sony 5 V outputs into ESP GPIO.
Do not use slow bidirectional I2C level-shifter modules for fast audio clocks.

Prefer removable links or lifting the identified source-side resistor ends
over cutting traces. Preserve relevant series resistors on the receiver side
and prevent contention with every original source being replaced.
This isolation requires an explicit hardware decision before proceeding.

**Exit condition:** verified isolation, levels and safe startup with buffers
disabled before enabling any ESP output into the receiver.

### 6. Validate audio using an ESP test tone, not a missing broadcast

With the required clocks and format established, supply a **low-level** test
tone through IC501's PCM input. Start with amplifier volume down and a verified
mute path; do not begin an audible test by assuming 0x0400 is safe full scale.
Follow the datasheet-supported initialization order rather than inventing a
sequence from the startup delays alone.

Check clock validity, mute states and analogue output progressively. This can
validate the conversion without a DSR signal once the original PCM source is
safely isolated. Only then test command effects, including attenuation, and
replace candidate interpretations with measured meanings.

If IC501 cannot be made to work after specific clock/format/control checks,
evaluate IC601 separately: its independent left/right data inputs make it a
different implementation, not a drop-in standard stereo DATA connection.

**Exit condition:** reproducible low-level analogue audio, controlled mute and
volume, and safe behavior across either ESP reboot and Sony power cycling.

## Immediate decision

The useful next work is **datasheet/net verification and firmware preparation**,
not another identical power-up or irreversible modification. Live replay and
trace isolation remain gated on the electrical and timing checks above.
