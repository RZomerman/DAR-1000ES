# DAR-1000ES bus capture findings

Bench session: 2026-11-02. Passive observations only; no working bus emulator
or electrically verified driver-ownership model is claimed.

**Interface scope:** These are the IC401/IC701 parallel-bus captures, not
the TD62 display-driver serial interface. Byte offsets below are not
display slot numbers, and handshake STB is not the drivers' FLSTB latch
signal. The later [optical display map](08-esp-display-driver-specification.md)
does not recover masked bus bits, establish full key codes or prove parallel
bus ownership. Those uncertainties remain valid.

## Capture wiring and alignment

Two independent 8-channel fx2lafw/Saleae-compatible analysers sampled at
2 MHz (0.5 us per sample). Physical channels start at 1; software channels at 0.

| Data analyser physical channel | Software | Signal |
|-------------------------------|----------|--------|
| CH1 | D0 | DBS7 |
| CH2 | D1 | DBS6 |
| CH3 | D2 | DBS5 |
| CH4 | D3 | DBS4 |
| CH5 | D4 | DBS3 |
| CH6 | D5 | DBS2 |
| CH7 | D6 | DBS1 |
| CH8 | D7 | DBS0 |

| Handshake analyser physical channel | Software | Signal |
|------------------------------------|----------|--------|
| CH1 | D0 | NCK (also called NACK during the session) |
| CH2 | D1 | REQ |
| CH3 | D2 | STB / STROBE |
| CH4 | D3 | ACK |
| CH5 | D4 | Duplicated DBS0, added after initial captures |

Data bytes require bit reversal from the raw analyser byte. Matching both
edges of duplicated DBS0 permits offset/drift alignment; clocks are not
hardware-synchronized. Observed rate ratio was approximately 0.999965.
Maximum fitted reference residuals were roughly 0.57-1.20 us across the
aligned captures. Occasional unmatched one-sample glitches were excluded.
Decoded values agreed at four positions per STB: near falling edge,
mid-pulse, near rising edge and ACK (or midpoint for an unacknowledged pulse).
This supports payload sampling, not sub-microsecond setup/hold certification.

Initial 2-second idle and 20-second startup recordings lacked the duplicate
reference and must not be treated as reliably combined byte captures.
Aligned startup/button captures used 60-second windows; the extended keypad
sequence used 90 seconds.

## Observed handshake and framing

Connected idle was HIGH on monitored lines. A transaction starts with
REQ falling, ACK falling, REQ rising, ACK rising. Each apparent payload
transfer follows STB falling, ACK falling, STB rising, ACK rising.

The first startup trace contained one short transaction with four acknowledged
STB transfers, and 33 long transactions with 32 acknowledged STB transfers.
Each ended with NCK falling before one final STB pulse, NCK rising, then STB
rising. The final STB had no additional ACK. Aligned captures consistently
sampled the previous value again on the closing strobe.

Thus 5/33 STB pulses correspond to apparent 4/32-byte payloads, not 5/33
independent data bytes. Some REQ-delimited regions contain no STB pulses;
their meaning is unresolved. The analysis uses REQ falling to delimit regions,
which is a parsing convention rather than a proven arbitration specification.

**NCK is associated with completion, not demonstrated busy/error handling.**
The prior proposal's negative-acknowledge/buffer-stall interpretation is not
supported by these captures. Direction and the controller driving each line
remain unproven.

Timing from the first startup recording (not universal limits):

| Measurement | Minimum / median / maximum |
|-------------|---------------------------|
| NCK LOW | 9.5 / 21.5 / 101 us |
| REQ LOW | 17.5 / 122 / 972 us |
| STB LOW | 11.5 / 41.5 / 182.5 us |
| ACK LOW | 9.5 / 16.5 / 101.5 us |
| STB falling to following ACK falling, within 1 ms search window | 9 / 37 / 178.5 us |
| STB rising to following ACK rising, within 1 ms search window | 4.5 / 11.5 / 97.5 us |

The following-edge timing search is not a proof of one-to-one pairing for
closing strobes. Long STB sequences span approximately 3.1-3.35 ms.
Regular updates are about 508 ms apart, with additional event-driven traffic.

## Capture integrity: missing information

Known strings match `observed_byte = ASCII | mask`:

- Earlier recordings: mask `0xD8`.
- After the operator checked/corrected probes: mask `0xD0`; DBS3 was recovered.

This is an empirical model for known text, not a reversible decoder or proof
that every control byte is distorted in precisely the same way. The remaining
suspect bits are DBS7, DBS6 and DBS4 (data CH1, CH2 and CH4). They may be
misprobed or electrically unreliable; the cause is not established.
Different true bytes can therefore appear identical, including a key press
matching release. Do not derive unique full key codes by simply clearing the
mask. Arbitrary ASCII cannot yet be reconstructed unambiguously.

## Fixed-layout display updates

Long messages repeat all 32 apparent bytes even when only one digit changes.
The evidence favors a fixed-state packet with implicit field locations,
rather than per-character cursor-position commands.

All offsets below are zero-based, excluding the closing STB:

| Offset(s) | Correlated role | Confidence/limitation |
|-----------|-----------------|-----------------------|
| 2-10 | Nine-character text field | Known ASCII strings fit after masking |
| 15-18 | Four-character numeric field | Right-aligned entry verified by 1, 12, 123, 1234 |
| 24 | LNC/auxiliary indicator control candidate | Controlled cycles change it |
| 30 | Other indicator/block/status candidate | Changes with MEMORY and BLOCK; not independently mapped |
| 31 | Checksum or control candidate | Correlates with payload changes; algorithm unknown |

Do not assume these fields exhaust the packet. `Memory OK` also modifies
positions outside the initial text region. The receiver's physical redraw
strategy is unknown.

Known text matches:

| Display content | Captured field |
|-----------------|----------------|
| `Frequency` | 2-10 |
| `   No DSR` | 2-10 |
| `   Memory` | 2-10 |
| ` OK` | 15-17 in the MEMORY confirmation |
| `1150`, `1227`, `1234` | 15-18 |

The display baseline included actual text `No DSR`, a red predefined block
letter, predefined SATELLITE/CABLE and LNC/OFF annunciators, frequency digits
and right-side dots. The later direct-driver inventory confirmed the signal
meter's bottom dots as slot 0 bit 14; their parallel-bus encoding is not
therefore established. Fixed labels
must not be mistaken for literal ASCII strings transmitted on the bus.

### MEMORY

One press enabled the predefined MEMORY indicator; the captured payload
alternated at offsets 30-31 about every 0.5 s for roughly five seconds.
A press/release pair appeared as a short nonidle/idle report.

Two presses produced the operator-confirmed text `MEMORY   OK`.
The second press was followed by a distinct long message within about 9 ms,
changing offsets 5-10, 15-21 and 31 relative to baseline. It lasted about two
seconds before the original payload returned. The exact character-to-offset
mapping beyond the known fields is not yet complete.

### BLOCK +

The operator confirmed red block C, `Frequency 1150Mhz`, SATELLITE.
Temporary text at offsets 2-10 returned to baseline about three seconds later;
changes at 14-17, 24 and 30-31 persisted. These correlated changes do not
individually prove which bits select block, source or numeric formatting.

### DIRECT and numeric entry

`Frequency`-compatible bytes alternate with space-compatible bytes at offsets
2-10 about every 508 ms. The host appears to implement blinking by rewriting
the field, not by an independently established local blink command.
The first DIRECT test contained multiple press/release-like sequences, so its
key attribution was ambiguous; the controlled numeric-entry test resolved a
DIRECT-associated report.

During the controlled 1, 2, 3, 4 sequence, offsets 15-18 progressed:

```text
"    " -> "   1" -> "  12" -> " 123" -> "1234"
```

The operator reported Frequency blinking and 1234 visible; automatic apply
after the fourth digit was not independently established.

### Frequency + and -

Three short + presses followed by three - presses produced ASCII-compatible
digits `1161 -> 1162 -> 1163 -> 1164 -> 1163 -> 1162 -> 1161`.
This supports 1 MHz steps in that mode, although the operator did not supply
an independent list of displayed values. Only offset 18 changed within the
numeric field. Byte 31 increased/decreased with the last digit, supporting
an additive-checksum candidate, not proving an algorithm. Simple XOR was not
consistent across all earlier masked captures. Frequency text returned to
No DSR about three seconds after the final adjustment.

### LNC

Operator-confirmed OFF -> A -> B -> OFF:

| State | Offset 24 | Offset 31 |
|-------|-----------|-----------|
| OFF | DE | DB |
| A | D6 | D3 |
| B | F6 | F3 |
| OFF restored | DE | DB |

Only these two apparent payload bytes changed. Values include capture masking.

### DISPLAY

Operator-confirmed normal+auxiliary -> dim+auxiliary -> bright without
auxiliary -> dim without auxiliary -> normal+auxiliary:

| Visibility | Offset 24 | Offset 31 |
|------------|-----------|-----------|
| Auxiliary visible, bright or dim | DD | F5 |
| Auxiliary hidden, bright or dim | D0 | F8 |

Text/frequency stayed constant. Brightness changes were invisible in sampled
bytes; they could use masked bits or local front-panel state. Do not infer
that brightness needs no protocol support.

## Observed key-state reports

Four apparent payload bytes usually have form `base XX base XX`, followed by
a closing STB repeating the last value. Release generally has all four bytes
equal to base. These are capture values, not verified complete key codes.
No hardware driver direction is inferred solely from this structure.

### Earlier captures: base D8

| Button/action | XX |
|---------------|----|
| MEMORY | ED (earliest capture), FD (later capture) |
| BLOCK + | FF |
| DIRECT, controlled 1234 test | FA |
| NEWS / digit 1 | D9 |
| AFFAIRS / digit 2 | DA |
| INFO / digit 3 | DB |
| SPORT / digit 4 | DC |
| EDUCATE | DD |
| DRAMA | DE |
| CULTURE | DF |
| SCIENCE | D8, identical to idle |
| VARIED | D9 |
| POP M | DA |
| ROCK M | DB |
| M.O.R. M | DC |
| MUSIC | FC |
| SPEECH | FD |
| BALANCE (MUSIC + SPEECH together) | FD |
| LIGHT M | DD |
| CLASSICS | DE |
| OTHER M | DF |
| UNDEFINED | F8 |

The differing MEMORY observations and collisions prohibit assigning unique
codes. They may reflect capture integrity, hidden bits or shared keys; not
all buttons' full identities are resolved.

### After probe correction: base D0

The operator confirmed the extended capture repeated the preceding 13 actions
(CULTURE through UNDEFINED, including BALANCE) before keys 1-16, MHz/CLEAR and
MONO MODE. There were 31 press/release pairs. One press matched idle.

| Button/action | XX |
|---------------|----|
| CULTURE | D7 |
| SCIENCE | D8 |
| VARIED | D9 |
| POP M | DA |
| ROCK M | DB |
| M.O.R. M | DC |
| MUSIC | FC |
| SPEECH | FD |
| BALANCE (MUSIC + SPEECH together) | FD |
| LIGHT M | DD |
| CLASSICS | DE |
| OTHER M | DF |
| UNDEFINED | F0 |
| 1-8, respectively | D1 D2 D3 D4 D5 D6 D7 D8 |
| 9-16, respectively | D9 DA DB DC DD DE DF D0 |
| MHz/CLEAR | F6 |
| MONO MODE | F0 |
| LNC | FB |
| DISPLAY | F4 |
| Frequency + | FE |
| Frequency - | FF |

The first NEWS-through-DRAMA test produced six press/release pairs with all
139 long messages unchanged. The remaining 13-action test similarly left all
138 long messages unchanged; extended-key capture left all 209 unchanged.
The operator reported no screen change. Lack of display response did not
mean the keys were electrically undetected.

## Startup with DBS disconnected

Only DBS0-7 were disconnected; handshake lines remained connected. The
operator identified both data/reference probes as being on the IC401 side.
The screen was blank. All eight sampled DBS lines and duplicated DBS0 stayed
HIGH with no transitions, preventing alignment/data decoding.

Handshake activity changed to approximately 538-us repeating cycles with
four STB pulses and five ACK pulses; NCK had no falling pulses. This is
consistent with an initialization/retry loop, not a confirmed interpretation.
It does not demonstrate IC401 sending ASCII while disconnected and challenges
the assumed ownership or probe-side identification. Reconfirm schematic
continuity and isolation before using this result to assign directions.

## Evidence artifacts and remaining work

Raw `.bin` captures, the decoder and decoded JSON were saved in the local
session workspace, not uploaded with this document. Raw files contain one
8-channel sample byte per 0.5 us; data and handshake files are separate.
Capture prefixes include:

```text
sony-idle-3-12 / sony-idle-3-13
sony-powerup-3-12 / sony-powerup-3-13
sony-aligned-powerup-retry
sony-button
sony-memory-confirm
sony-block-plus
sony-direct
sony-direct-1234
sony-dbs-isolated-startup
sony-pty-buttons
sony-other-buttons
sony-keys-1-16-clear-mono
sony-lnc-cycle
sony-display-cycle
sony-frequency-plus-minus
```

Resolve DBS7/DBS6/DBS4 capture integrity, verify controller ownership and
voltage/interface requirements, then repeat representative keys and display
changes. Determine the full packet header, control bits and checksum only
after trustworthy complete bytes are available. Keep all active-drive
experiments isolated until safe direction/turnaround behavior is established.
