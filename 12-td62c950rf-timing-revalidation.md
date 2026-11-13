# TD62C950RF Timing Revalidation

Rechecked 2026-11-10 against the Toshiba
[TD62C950RF/LF datasheet](https://github.com/RZomerman/DAR-1000ES/blob/main/TD62C950RF.PDF),
the original Sony scan capture, and
the 24 MS/s capture of the experimental 250 Hz MEMORY scan. This chapter
separates datasheet limits from measured digital timing; it is not an
analogue signal-integrity certification.

**Later operating baseline:** Firmware 0.3.25/0.3.26 uses AHCT v2 wiring,
shared STB on both TD chips and guarded timings. Clean full-width text and
scrolling were subsequently observed, with independent sampled TD checks.
See the [screen-driver operating guide](13-screen-driver-operating-guide.md)
for those results and the remaining electrical/current limitations. The
historical failed 250 Hz capture below is retained unchanged.

**Scope after the 2026-11-13 inventory:** The 250 Hz results below describe
the historical firmware 0.3.14 capture, not the later 0.3.23 held-grid
configuration. The [current control guide](08-esp-display-driver-specification.md)
and [optical validation history](06-direct-display-driving.md) establish
element identities, not timing compliance. Neither the camera map nor the
3.75% held-grid duty guard resolves the historical STB anomaly or certifies
driver-input voltages, analogue edges or VFD current limits.

## Datasheet limits

Use the **recommended operating conditions on page 5** when selecting
driver settings, not the looser 25 C switching-test figures on page 7:

| Recommended condition, Ta = -40..85 C | Limit |
|---|---:|
| Logic supply VDD | 4.5-5.5 V |
| Applied CK frequency | At most 8 MHz |
| CK pulse width | At least 75 ns |
| DATA setup before CK rising | At least 50 ns |
| DATA hold after CK rising | At least 50 ns |

These are not VFD brightness, grid-duty or current-limit specifications.

### Switching-test characteristics, not replacement operating limits

The switching-characteristics table on datasheet page 7 is specified at
Ta = 25 C, VDD = 5 V, HVCC = 70 V, VIH = VDD and VIL = GND. The listed output
test loads are CL(OUTn) = 50 pF with RL = 1.5 kOhm and CL(S-out) = 15 pF.
Page 9 defines the timing reference edges.

| TD62C950RF parameter | Datasheet value | Meaning for this scan |
|---|---:|---|
| Maximum operating clock frequency, f(max) | Minimum f(max) 9 MHz; typical 17 MHz under these test conditions | Does not replace the recommended 8 MHz operating maximum |
| CK minimum pulse width | 70 ns | Test-condition figure; use the recommended minimum 75 ns |
| STB minimum pulse width | 70 ns | Both levels must be long enough to constitute valid pulses |
| DATA setup before CK rising | 20 ns | Test-condition figure; use the recommended minimum 50 ns |
| DATA hold after CK rising | 20 ns | Test-condition figure; use the recommended minimum 50 ns |
| CK rising to STB falling setup | 50 ns | Required for the latch/shift sequence |
| Input rise time maximum | 1.0 us | Digital logic captures cannot measure this analogue edge time |
| Input fall time maximum | 1.0 us | Digital logic captures cannot measure this analogue edge time |
| VIH / VIL | At least 0.70 VDD / at most 0.30 VDD | At VDD = 5 V: HIGH >= 3.5 V; LOW <= 1.5 V |

The datasheet does not specify the display's frame refresh rate or multiplex
duty cycle. The 250 Hz frame target is therefore an application-level
experiment, not a TD62C950RF clock-frequency requirement.

## Original Sony timing baseline

The original six-signal capture
(`sony-td-satellite-lnc-off.bin`, 8 MHz sample rate; channel order documented
in [Chapter 6](06-direct-display-driving.md)) was re-counted:

| Measurement | Original Sony capture |
|---|---:|
| Segment shifts per slot | 40 |
| Grid shifts per slot | 1 |
| Mean slot interval | 301.064 us |
| Mean 26-slot frame | 7.82765 ms |
| Frame refresh | 127.75 Hz |
| STB HIGH pulse | 2.750-2.875 us |

The original serial-clock pulse widths were approximately 0.375-0.5 us in
the decoded capture. These are above the recommended 75 ns CK minimum, but
the original capture's 125 ns sample period cannot certify near-limit
50 ns DATA setup/hold timing or analogue edge quality.

## Experimental 250 Hz timing

Firmware 0.3.14 uses the LCD i80 peripheral at an 8 MHz waveform sample rate.
That is **not** the TD62C950RF shift-clock frequency. Each slot is resampled to
1,230 samples:

| Measurement | Configured or captured value |
|---|---:|
| Samples per slot | 1,230 at 8 MHz |
| Nominal slot interval | 153.750 us |
| Nominal 26-slot frame | 3.9975 ms |
| Nominal frame rate | 250.16 Hz |
| Repeat-capture valid slots | 25,461 slots with 40 segment-clock rises |
| Captured mean slot interval | 153.759 us (153.625-153.917 us) |
| Captured frame/data cadence | About 250 Hz; the repeated MEMORY word measured 249.7 Hz |
| FLSCLK rising-to-rising interval | 500 ns median; 333 ns shortest in the active scan |
| FLSCLK pulse widths | 250 ns median HIGH and LOW; shortest sampled HIGH 125 ns, LOW 83 ns |
| Grid-clock interval | One clock per 153.76 us slot; its short level was about 1 us |
| Dim FLCLEAR-low interval | About 99-100 us in the repeat capture |
| Output sample resolution | 125 ns at 8 MHz; Saleae capture resolution 41.7 ns at 24 MS/s |

The measured FLSCLK cadence is about 2 MHz typically and at most about 3 MHz
from the shortest active-scan rising-edge interval. That is below the
recommended 8 MHz operating maximum. The shortest sampled FLSCLK LOW pulse,
however, is only about 83 ns: approximately 8 ns above the recommended
75 ns minimum (13 ns above the page-7 test figure). Given the 41.7 ns sample
interval and unmeasured analogue
edge shape, this is not enough margin to certify the physical pulse width.
Most observed clock pulses are around 250 ns.

The dim CLEAR interval is intentionally compressed with the slot: the
approximately 201 us source delay becomes roughly 103 us nominally. The
captured approximately 99-100 us interval is consistent with that
time-compressed target, not with the original Sony dim timing.

## STB and data-timing caveat

The repeat `.sr` capture metadata labels channels D0-D7 but does not embed
signal names. Using the session's recorded wiring map (D3 = GPIO8 / FLSTB,
D4 = GPIO9 / FLSDATA, D5 = GPIO10 / FLSCLK), the capture has a significant
anomaly:

- The channel mapped to STB has a median of **two rising transitions per
  grid-clock slot**, rather than the single intended STB pulse per slot.
- Its median HIGH run is one 24 MS/s sample, about **41.7 ns**, below the
  datasheet's 70 ns minimum pulse width; about 54% of its recorded HIGH runs
  are this short.
- The generated waveform's nominal STB pulse would be roughly 1.4 us after
  time scaling from the original 2.75-2.875 us pulse.

If that channel assignment is correct, the captured STB waveform does **not**
validate compliant operation. The discrepancy could be a probe/channel-map
error, buffer-side edge artifact, or real signal distortion; the `.sr`
metadata alone cannot distinguish these. The waveform unit test for that build
checks the generated in-memory template, not the physical STB pin.

These captures do not establish the recommended 50 ns DATA setup/hold or
the 50 ns CK-to-STB setup requirement at the driver inputs. One-sample
timing resolution is 41.7 ns, and transitions recorded in the same sample
are unresolved; the looser page-7 DATA figures are only 20 ns.
Digital captures likewise cannot verify maximum analogue rise/fall times.

## Conclusion and next validation

The 26-slot cadence, 40 segment clocks per slot, approximately 250 Hz refresh,
FLSCLK frequency, and compressed dim interval are consistent with the
configured experiment. The FLSCLK frequency is below the TD62C950RF limit,
but the pulse-width margin is narrow at its shortest observed value.

The complete interface cannot yet be declared timing-compliant: the assumed
STB channel shows extra, often sub-70 ns pulses, and the recommended 50 ns
DATA setup/hold,
50 ns CK-to-STB setup, analogue edge times, and voltage thresholds have not
been verified at the IC inputs. Before further timing changes, verify the
analyser channel wiring and probe both sides of the translator. Use a
high-bandwidth oscilloscope or a substantially faster logic capture at the
TD62 inputs to check the short STB events and edge timing. Keep translator
OE HIGH in the installed always-enabled arrangement. Before any USB reset
or reflash, confirm all six Sony display signal wires are physically
disconnected; switching Sony off alone is not electrical isolation.
