# Keypad mapping observations

2026-11-14: The isolated passive matrix was scanned with a TCA8418 configured
for rows R0-R5 and columns C0-C7. Row and column numbers are zero-based.
In the latest full run, buttons were pressed individually in the order shown.
Each reported one press and one release. Entries are observations, not
firmware assignments.

**Wiring note:** The original column pull-down resistor paths need to be
broken or isolated when connecting the matrix to the TCA8418. The TCA8418
drives the scanned columns itself; an external pull-down can make a pressed
row appear active during every column scan. Make wiring changes with all
power disconnected, and verify isolation and switch continuity before
reconnecting power.

| Button | Observed press and release |
| --- | --- |
| 1 | R0-C4 |
| 2 | R0-C5 |
| 3 | R0-C6 |
| 4 | R0-C7 |
| 5 | R0-C0 |
| 6 | R0-C1 |
| 7 | R0-C2 |
| 8 | R0-C3 |
| 9 | R1-C4 |
| 10 | R1-C5 |
| 11 | R1-C6 |
| 12 | R1-C7 |
| 13 | R1-C0 |
| 14 | R1-C1 |
| 15 | R1-C2 |
| 16 | R1-C3 |
| MHz/Clear | R5-C1 |
| MonoMode | R2-C3 |
| Display Mode | R5-C7 |

Earlier tests reported paired C1/C2 events and missing events, including for
Culture, OtherM and BLOCK+. The user identifies those three as sharing a
physical matrix column with buttons 7 and 15. The latest full run resolved
the paired and missing events for buttons 1-16, MHz/Clear and MonoMode.

No I2C errors or FIFO overflows were reported in the latest run.

## Additional buttons

The following observations were captured in a second ordered run. Entries
marked "ordered run" are tentative: missing or spurious events can shift
the association. Entries marked "individual" were retested separately.

| Button | Observation | Evidence |
| --- | --- | --- |
| NEWS | R3-C4 | Five-button retest |
| AFFAIRS | R3-C5 | Ordered run |
| INFO | R3-C6 | Ordered run |
| SPORT | R3-C7 | Ordered run; two quick press/release pairs |
| EDUCATE | R3-C0 | Ordered run |
| DRAMA | R3-C1 | Individual |
| CULTURE | R3-C2 | Individual |
| SCIENCE | All eight R3 columns together | Individual and five-button retest; unresolved |
| VARIED | R4-C4 | Five-button retest |
| POPM | R4-C5 | Ordered run |
| ROCKM | R4-C6 | Ordered run |
| MORM | R4-C7 | Ordered run |
| Music | R2-C7 | Ordered run |
| Speech | R2-C0 | Ordered run |
| LightM | R4-C0 | Ordered run |
| Classics | R4-C1 | Ordered run |
| OtherM | R4-C2 | Individual |
| Undefined | All eight R4 columns together | Individual and five-button retest; unresolved |
| BLOCK- | All eight R5 columns together | Individual and five-button retest; unresolved |
| BLOCK+ | R5-C2 | Individual; two quick press/release pairs |
| Memory | R5-C0 | Individual; two quick press/release pairs |

No I2C errors or FIFO overflows were reported during the individual checks.
The cause of the row-wide events and rapid repeated pairs has not been
established.

In the later five-button retest, NEWS and VARIED each produced a single
correct press/release pair. SCIENCE, Undefined and BLOCK- continued to
produce events for all eight columns of their respective rows.

With power disconnected, the user removed the matrix wires from breakout R3
and C3. After power was restored, two direct R3-C3 jumper tests under the same
6x8 scan produced only R3-C3 events (raw 0xA2 press and 0x22 release); one
connection included a brief extra release/press. This supports correct
operation of that scan path in isolation, but does not locate the fault in
the connected matrix.

After the user reconnected R3 and C3 with power off, SCIENCE was pressed
twice. Each press again produced events for all eight columns of R3.
In a subsequent individual ordered test of buttons 8 and 16, each similarly
produced all eight column events on R0 and R1 respectively. This differs from
the earlier clean full run and means their previously observed R0-C3 and
R1-C3 mappings no longer correspond to clean current behavior.

The user then moved the shared matrix wire from C3 to C8, leaving R3
unchanged and C3 empty. With the scanner set to 6x9 (R0-R5, C0-C8), SCIENCE
produced press and release events for all nine columns of R3. The user
reported pressing twice, but only one group of press/release events was
recorded. The issue therefore persisted on C8; the C3 pin alone does not
explain it.

With the same 6x9 scan, individually retested 8, 16, UNDEFINED and BLOCK-
each triggered all nine columns on R0, R1, R4 and R5 respectively. MONO
MODE also triggered all nine columns on R2 in the ordered run. Together
with the earlier SCIENCE test, the row-wide behavior is observed for all
six buttons sharing the moved matrix column.

On a later retest, 8 and 16 each produced one clean press/release pair at
R0-C8 and R1-C8 respectively. SCIENCE, UNDEFINED and BLOCK- were not
retested at that time, at the user's request.

A subsequent BLOCK- retest with 6x9 scanning again produced all nine
column press/release events on R5. The next individual test of 8 produced
simultaneous press/release events at R0-C2 and R0-C8, unlike its earlier
clean R0-C8 test.

With the same 6x9 configuration, the user tested 1-7 and 9. Each produced
one press/release pair at its previously observed position. An additional
R1-C5 press/release (previously mapped to 10) also appeared; its cause was
not confirmed.

Display Mode was tested twice under 6x9 scanning and produced two clean
R5-C7 press/release pairs. With confirmed matrix scanning enabled, a short
press starts Now Playing when stopped, cycles modes, then stops at OFF;
a long press cycles DIM once while held, when a radio scan is running.
CHECK was subsequently confirmed twice at R2-C4 (code 25). It manually starts
Stereo VU only when stopped, restoring brightness from before OFF (initially
1.2% after reboot). Press CHECK only with Sony powered,
original drivers isolated and AHCT outputs ready; software cannot sense these
conditions. It does nothing to a running scan or a non-AHCT pin profile.
The same hardware readiness is required before starting with Display Mode.
Buttons 1–16 select their individually assigned radio presets. Assignments are
configured on the radio page; unassigned buttons leave the station unchanged
and show "Unassigned" briefly. Assigned buttons show the preset, search, then
the connected station for two seconds before returning to the display mode.
The keypad page
can remember the confirmed isolated 6 × 9 matrix for automatic boot scanning;
disable that setting before changing the wiring.

## 2026-11-15 column retest

The user confirmed the shared wire remains on C8, C3 is empty and the matrix
is isolated from the original controller. Under 6x9 scanning, individual
presses of BLOCK-, 8, 16, SCIENCE, UNDEFINED and MONO MODE each produced
press/release events for all nine columns on R5, R0, R1, R3, R4 and R2
respectively. No I2C errors or FIFO overflows were reported.
An additional R2-C6 press/release occurred between the UNDEFINED and MONO
MODE checks; its source was not established.

### Other-line checks

Subsequent checks under the same 6x9 scan produced one correct press/release
pair per requested button:

- R0: 1-7 mapped to C4, C5, C6, C7, C0, C1, C2.
- C1: 6, 14, DRAMA, CLASSICS, MHz/Clear mapped to R0, R1, R3, R4, R5.
- C2: 7, 15, CULTURE, OtherM, BLOCK+ mapped to R0, R1, R3, R4, R5.
- C4: 1, 9, NEWS, VARIED mapped to R0, R1, R3, R4.
- R2: Music mapped to C7 and Speech to C0.

This demonstrates normal responses on all six row inputs and the seven
other connected columns for the tested switches. It does not establish
every switch or rule out intermittent faults. Unrelated R2-C6 events
appeared before the 1-7 sequence. No I2C errors or FIFO overflows occurred.

A subsequent individual button 8 retest produced a clean R0-C8 press and
release, unlike its preceding all-columns result.

The next complete shared-column retest produced one clean press/release pair
per button: 8 R0-C8, 16 R1-C8, SCIENCE R3-C8, UNDEFINED R4-C8,
BLOCK- R5-C8 and MONO MODE R2-C8. No extra events, I2C errors or FIFO
overflows occurred. Prior intermittent failures mean continued observation
is warranted.
