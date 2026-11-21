# Startup procedure: proposed sequence and user instructions

## Status and scope

Controller firmware 0.3.49 adds a **manual, isolated bench test** of the
display artwork only. It does not change boot behavior. With Sony-side
display signals disconnected and the buffers disabled, stop output, select
AHCT v2, confirm isolation on `/screen`, then click **Arm MONO startup test**.
Press physical or virtual **MONO MODE** within 30 seconds; the virtual keypad
must be armed separately. Each arm allows one run. The controller queues a
blank matrix at the end and returns to stopped output. This does not start
audio, tune a station, or restore saved state. Do not reconnect the Sony
display for this test: its full-fill load limit remains unvalidated.

**The automatic procedure below is a design specification.** It specifies a
future authorized power-on sequence. It does not implement firmware, establish
an electrical interlock, or authorize connected reset, flashing or automatic
display start. The present AHCT-v2 display must remain **boot-stopped** until
a verified hardware interlock exists. “Release the screen” means release an
animation only after electrical readiness, not bypass the hardware-ready check.

The requested experience is:

1. The user powers the unit on; the ESPs boot.
2. Once display hardware is safely ready, falling-pixel snow fills the whole
   matrix area.
3. Snow falls away to reveal `SONY DAR-1000ES`; hold the title briefly.
4. Star-like dot noise replaces the title, revealing **E** and **S** first in
   their final positions. The remaining letters build `Elevated Standard`.
5. Hold the full phrase, then dissolve it back into star-like dots. Clear
   every dot and show a blank matrix before releasing the overlay.
6. Restore the last used radio station, brightness and selected display mode
   from a persistent status file; do not reset playback to NPO Radio 2.

The sequence/content and persistence of all three selections above are user
requirements. Timings, density, startup-artwork brightness limits, skip
controls and when station audio starts remain recommendations or open decisions.
The startup sound direction is light, short and high fidelity: high bells for
the snow entry and title reveal, and a soft, airy swoosh for the star transition.

### Current implementation versus target

| Area | Current repository behavior | Proposed target |
| --- | --- | --- |
| Display boot | AHCT-v2 GPIO profile, output stopped; no Sony power or display-buffer `/OE` sensing/control | Automatic sequence only after verified independent hardware protection and software readiness |
| Radio boot | Controller-local radio defaults to NPO Radio 2 | Restore the last used station from the status file, not the hardcoded boot station |
| Display memory | Mode/DIM not restored across reboot; initial radio brightness is 1.2% per-grid duty; pre-OFF brightness is remembered only in RAM | Restore saved brightness and selected mode, including OFF, from the status file; artwork load limits still need electrical validation |
| Saved radio data | Custom URL and assigned preset records are stored in NVS; this is not last-station restore | Persistent status file containing station identity/name/direct stream URL, brightness and display mode; never rewrite presets during startup |
| Two ESPs | Controller still decodes radio locally; UART is diagnostic PING/PONG/MAP/ACK, not playback control | Controller renders/keypad/UI; audio ESP eventually connects, decodes and reports actual playback |
| Power button | POWER mapping unverified/disabled in the 0.3.48 virtual panel | Verified power-event detection/coordination remains to be designed |
| Startup artwork | 0.3.49 prepares the manual, isolated MONO bench test above | Temporary, bounded matrix overlay; no persistent “startup display mode” |

Primary evidence: [operating guide](13-screen-driver-operating-guide.md),
[display specification](08-esp-display-driver-specification.md) and
[split architecture](15-split-setup-architecture.md). Chapter 08 includes
historical wiring/timing limitations; chapter 13 is the current AHCT operating
reference. Firmware evidence is in
[`sony_screen.c`](firmware/main/sony_screen.c) (`sony_screen_init`, mode/DIM
and stopped-output guards),
[`radio.c`](firmware/main/audio/radio.c) (`selected`, `radio_init`, NVS preset
storage and selection generations), and
[`settings.c`](firmware/main/settings.c) (other saved settings, not a startup
station/display restore mechanism).

## Display content and fit

There are **22 cells, each 5 pixels wide by 7 high**: a logical **110 x 7**
canvas, 770 addressable matrix dots, with physical gaps between cells.
There is no eighth row, second text line or continuous phosphor across gaps.
Use the existing slot orientation: leftmost cell is slot 22, rightmost slot 1;
within a cell `bit = row * 5 + column`, top-left origin.

| Content | ASCII glyphs, including spaces/hyphen | Placement in cells, numbered left to right |
| --- | ---: | --- |
| `SONY DAR-1000ES` | 15 | Text in 4-18; three blank cells left, four right |
| `Elevated Standard` | 17 | Text in 2-18; one blank cell left, four right (browser preview) |

Render the exact case and ASCII hyphen shown, using the existing 5x7 font.
The 15-cell title leaves seven spare cells: exact symmetric whole-cell
centering is impossible; the placement above chooses the left-biased nearest
center. The tagline uses the browser preview's placement: **E** in cell 2 and
**S** in cell 11. Both stay in place as the other letters appear.
Each phrase fits on its own across one row of cells without horizontal
scrolling. Apply effects to matrix dots only.

- **Fill:** spawn falling dots across all 110 logical columns and accumulate
  snow through all seven rows until the entire matrix area is filled.
  Physical cell gaps remain dark; fixed icons, numeric fields and meters
  are not snow and must not be all illuminated.
- **Drain / title:** drain the snow down beyond row 6, uncovering a stationary
  title mask. Hold the unobscured title before starting the star reveal.
- **Star reveal:** replace the title with sparse, twinkling dots. Resolve **E**
  and **S** first, then build the remaining letters from left to right while
  the noise thins out. Hold the full phrase without noise.
- **Star dissolve / blank:** break the letters back into twinkling dots, let
  the dots fade out, and submit a fully blank matrix frame before handing off
  to connection status or the restored display mode. On cancellation or safety
  loss, use the existing stop path immediately; do not wait for a blank frame.

Keep fixed-element fields blank during the artwork; after it, use only
truthful source/status indicators. Preserve the existing 26-slot scan,
transfer/blanking timing and physical stop/output-disable mechanisms.

### Recommended animation budget (not settled)

Use elapsed-time scheduling rather than blocking sleeps or per-dot work in
the real-time scan. The browser preview uses a **6.25 s** timeline: snow fill
1.25 s, drain/title reveal 0.90 s, title hold 0.85 s, star reveal 1.25 s,
phrase hold 1.00 s, and star dissolve 1.00 s, ending on a blank frame.
Recommended hard artwork deadline: **7 s from its first frame**; finish or
abort the overlay by then regardless of random snow distribution or network
state. Schedule the blank frame within the deadline on normal completion.
The full-fill endpoint must be deterministic and brief, not wait indefinitely
for every randomly chosen dot.

Snow and star density, easing, refresh/update rate and dwell need visual review
on the actual seven-row display. Brightness is **not selected here**: use only
a subsequently validated startup duty/load limit, not a guessed percentage.
Existing low-duty text/held-grid tests do not certify a full 770-dot frame.
Unchanged per-grid duty does not mean unchanged aggregate current. If full
fill cannot meet validated electrical limits, keep it disabled and revisit
the specification rather than silently treating sparse snow as full fill.

## Startup sound direction

**Required creative direction, not installed behavior:** a short,
light, high-fidelity sonic signature synchronized with the animation. Use
delicate high bells in the first and second stages, followed by a soft swoosh
for the final stage. Avoid a loud boot jingle, heavy bass impact, harsh
high-frequency transients or a long reverb tail.

The following cue placement is a recommendation for auditioning, not locked
timing:

| Visual stage | Proposed sound |
| --- | --- |
| Snow starts falling and filling the screen | A sparse, soft high-bell shimmer; leave space between notes rather than sounding every falling dot. |
| Snow falls away to reveal `SONY DAR-1000ES` | A second clear, complementary bell accent as the name becomes readable, with a short, clean decay. |
| Stars reveal `Elevated Standard`, then dissolve to blank | Audition a brief, airy swoosh for the star transition; let the tail finish by the return to normal display. No impact or additional loud sting. |

The browser preview uses the repository's
[`media/dar-startup-sound-v1.wav`](media/dar-startup-sound-v1.wav), a 6.8 s
stereo PCM clip, synchronized to the preview timeline. This is a candidate for
audition, not an approved firmware asset, playback level or cue mix.

Keep the complete signature within the artwork sequence; its tail should
finish before normal station listening begins in the recommended mix. Exact
notes, bell timbre, level, cue durations, stereo width and reverb require an
audio/visual audition. High fidelity means a clean, unclipped source and
playback path, not excessive treble or loudness. Prefer an original lossless
PCM master and avoid unnecessary lossy encode/decode stages; the delivery
format must match the eventual audio implementation. The target split-audio
path is 48 kHz, but asset format, bit depth and decoder support are not
established by this specification.

### Synchronization and playback boundaries

- Trigger cues from the same elapsed-time phase timeline as the animation,
  not unrelated delays or network arrival times. Do not send PCM over the
  diagnostic UART; future audio-side cue scheduling/control is unimplemented.
- Use a locally available asset or synthesis so the signature does not
  depend on the radio directory or internet connection. Do not block the
  display waiting for audio: skip unavailable/late cues rather than replaying
  them after their matching stage, and expose audio startup failures.
- Play once per authorized startup. Stream reconnects, browser refreshes and
  audio-link retries must not repeat the signature. Stop/cancellation or loss
  of audio authorization must cancel remaining cues and end audio cleanly.
- Keep station restoration independent of the signature. Recommended:
  connect the saved station in parallel, hold its audible output until the
  signature finishes, then transition without overlapping programme audio
  and bells. Audible-start policy remains open; it must not change the saved
  station, display mode or brightness.
- Startup level, mute/disable preference, playback volume relationship and
  maximum output level remain open. Do not assume a stored radio volume is
  suitable for the signature or automatically enable unverified audio
  hardware. Respect separately established audio readiness and mute/relay
  safeguards.

## Developer-facing startup state machine

These are proposed logical states, not existing API commands. Hardware
permission and playback readiness are independent.

```text
POWER-ON / cold boot
        |
        v
INHIBITED ---- gate/init fault or deadline ----> SAFE-STOPPED
        |
        | verified hardware permission + controller ready
        | + accepted saved-OFF policy
        v
SNOW-FILL -> DRAIN / TITLE -> TITLE-HOLD -> STAR-REVEAL
        -> PHRASE-HOLD -> STAR-DISSOLVE -> BLANK -> RESTORE-STATUS

Artwork deadline: abort unfinished artwork -> RESTORE-STATUS
RESTORE-STATUS -> current-generation playback/data valid -> RESTORED-MODE

Any state: gate loss / Stop / safety action -> SAFE-STOPPED
Parallel: load saved station -> audio/link wait -> connecting -> playing/error
```

| State | Entry/output and readiness conditions | Exit/failure target |
| --- | --- | --- |
| INHIBITED | No display output. Validate settings and initialize bounded rendering tasks. Independently request the saved radio station when the playback service is ready. | Start artwork only with a fresh authorized boot event, verified hardware permission, controller scan readiness and resolved OFF policy. Timeout remains stopped. |
| SNOW-FILL / DRAIN / TITLE / TITLE-HOLD / STAR-REVEAL / PHRASE-HOLD / STAR-DISSOLVE / BLANK | Temporary matrix overlay under current output authorization; no preference writes. Check cancellation/authorization before every submission. | Advance on finite deadlines, not network events. On normal completion, present BLANK before RESTORE-STATUS. Any safety loss invalidates queued work immediately. |
| RESTORE-STATUS | Release artwork. If still connecting, show `Connecting`, `Retrying` or `Offline` as appropriate, subject to OFF/stop state. | Current-generation valid data permits the selected mode. Failure stays explicit; no stale “playing” state. |
| RESTORED-MODE | Render the saved mode with truthful current data; unavailable metadata gets explicit unavailable/station fallback, never invented song details. | Normal UI takes over; future reconnects do not replay artwork. |
| SAFE-STOPPED | Stop scan through existing stop path; independent hardware gate disables outputs on electrical faults. Clear overlay ownership and pending startup submissions. | No automatic retry/frame may restart output. Require a fresh valid authorization/recovery action under the eventual interlock policy. |

Only an actual authorized boot gets this sequence. Wi-Fi reconnect, stream
retry, UART recovery, mode/preset changes, browser refresh and travel-demo page
load must not create another boot event. A controller reset is not proof of
a fresh Sony power-on: return inhibited, distinguish the reset cause, and do
not automatically replay/re-enable until the recovery policy is verified.

Use an output-authorization epoch/cancellation token as well as a station
generation. Stop, disarm, gate loss or ownership change invalidates all older
animation submissions. A queued frame cannot reacquire scan ownership or
undo OFF/Stop. Account for existing DMA stop latency (chapter 13 records
0.28 s); software cancellation is not an instantaneous physical cutoff.

### ESP coordination and bounded failure

The controller owns display rendering, keypad dispatch, preferences and
temporary overlay arbitration. The target audio ESP owns stream/audio
readiness, not display electrical authorization. A future versioned
`READY`/handshake exchange should identify the current boot/session and
supported protocol, with bounded acknowledgments and timeouts. **READY is a
proposed protocol concept, not a command in today's diagnostic UART.**
PING/PONG alone does not establish playback readiness; MAP must never be
used as startup sequencing.

Do not assume simultaneous ESP boot. Recommended initial service-wait budget:
10 s before reporting audio-link unavailable, with bounded background retry
(for example 2 s increasing to a 30 s ceiling; exact policy open). The
hardware gate's stability and fault-response deadlines instead require
measured electrical limits, not these UI timing suggestions.

A display-ready controller may complete artwork while audio/network is late.
Afterward show truthful waiting/failure status on authorized output; expose
diagnostics through the web UI if the physical display is inhibited. A missing
or incompatible audio ESP must not hold the animation forever, enable audio
buffers, fabricate telemetry or trigger a display restart. Until migration,
these readiness distinctions apply to the controller-local radio service.

Audio may connect in parallel; whether audible station playback begins immediately
or after the tagline/startup sound is **open**. The recommended mix completes
the signature before station audio becomes audible. Neither choice authorizes unmute, relay
operation or audio `/OE`. Chapter 15's audio chip-4 enable covers multiple
outputs; mute/relay asserted levels remain unverified. Software/audio-ready
is never electrical authorization, and this chapter specifies no relay,
power-hold or mute-polarity sequence.

## Restore, persistence and errors

- **Required:** persist the last used radio station, brightness and selected
  display mode in a **status file**, and restore all three on normal boot.
  Do not substitute generic NVS-only storage or reset playback to NPO Radio 2.
  Current firmware does not implement this requirement.
- Save intentional station, brightness/DIM and display-mode changes, not frames,
  animation phases, transient status, metadata changes or retry events.
  Keep the saved preference separate from the active temporary overlay.
  Persist selections with bounded/coalesced writes and validated,
  versioned records resilient to interrupted writes.
- Retain stable station identity, name and a direct playable HTTP/HTTPS stream
  URL, including custom selections, sufficient to restore independently of
  directory availability or preset numbering. A directory lookup or preset
  index alone is insufficient. Editing/deleting a preset must not silently
  replace the saved station; explicit deletion/replacement policy is open.
- Loading preferences must not assign/erase presets, change their numbering,
  replay a physical preset announcement as though a key was pressed, or
  overwrite the country filter. Country-filter persistence is not agreed.
  User selection during startup supersedes the pending restore and its
  generation; startup must not later revert that choice.
- Restore the selected radio station, not an assumed “last source.”
  Radio-versus-AirPlay restoration/arbitration remains open.
- **Saved display OFF must be respected.** Do not silently restore Now
  Playing. Whether a one-time artwork sequence may precede saved OFF is
  unresolved and blocks final automatic-start behavior; no override is
  authorized by this specification. Any permitted artwork must still end
  in OFF, without status or metadata restarting output.
- **Restore saved brightness as well as mode.** Brightness persistence is now
  required, not an open decision; today's RAM-only brightness memory does not
  satisfy it. Store the intended normal-display brightness separately from
  any temporary artwork load limit. Electrical guard validation still applies:
  if a saved value is invalid/unsupported, report it and keep output inhibited
  rather than silently overwrite the preference or bypass a safety limit.

### Proposed status-file contract, not an existing implementation

Use the existing `/spiffs` mount convention. A suitable **proposed** canonical
path is `/spiffs/startup-status.json`, separate from web assets and presets.
It is runtime user data, not a factory-default file to ship in `firmware/data/`.
The controller owns it in the target split setup and sends its restored station
to the audio ESP; the audio ESP must not independently choose a boot station.

Proposed version-1 JSON schema:

| Required field | Type / meaning |
| --- | --- |
| `version` | Integer `1`; reject unsupported schema versions explicitly |
| `station.id` | Bounded nonempty stable identity, independent of list position; support locally assigned identities for custom URLs |
| `station.name` | Bounded station display name from the selected record |
| `station.url` | Validated direct playable HTTP/HTTPS stream URL, not a directory/search/page URL |
| `brightness.per_grid_duty_ppm` | Integer normal-display per-grid duty in parts per million; validate against supported, electrically qualified settings |
| `display_mode` | Supported display-mode identifier from chapter 13, or explicit `off` |

For unit clarity, 0.75% is 7,500 ppm; this is a conversion example, not a new
brightness recommendation or full-snow load approval. OFF does not discard
the saved brightness. Never write `startup`, connection status or animation
frames as the selected mode. Bound record/string sizes before allocation,
validate all fields together, and reject malformed/partial records explicitly.

Filesystem evidence:
[`spiffs_storage.c`](firmware/components/spiffs_storage/spiffs_storage.c)
mounts partition `storage` at `/spiffs` and currently enables formatting on
mount failure. The [filesystem guide](firmware/docs/reference/spiffs.md) and
[`web_server.c`](firmware/main/network/web_server.c) document/implement file
access and ESP log/HTTP error reporting. The upload handler opens its
destination directly with `wb`; it is **not an atomic status-file replacement
helper**, and no such existing helper was found in the inspected storage code.

Required save behavior is atomic replacement of a complete validated snapshot:
serialize changes through a shared storage helper; write a sibling candidate
file, check every write/flush/close result, then commit a replacement while
retaining the last good record on failure. Verify the actual SPIFFS/VFS
replacement and power-loss guarantees before selecting rename/commit details;
do not assume POSIX-style atomicity or durability. If those guarantees are
insufficient, design a recoverable generation/last-good scheme before release.
Do not truncate the canonical file in place or use the generic upload endpoint
as the runtime save path.

Follow repository error-reporting conventions: return an `esp_err_t` failure,
log the cause with `ESP_LOGE`, and expose persistence errors through the relevant
UI/API rather than acknowledge unsaved state as durable. An accepted in-memory
selection may remain usable after a write failure, but must be marked unsaved;
retain the last durable file and retry without frame-by-frame writes. Check
space/exhaustion, interrupted writes and concurrent user changes; an older save
must not replace a newer snapshot. Review mount-failure autoformat and filesystem
image update behavior so status loss is not silent. First boot, missing/corrupt
file, unsupported schema and irrecoverable storage failure require explicit
restore/setup status; their fallback policy remains open, **never a silent
default to NPO Radio 2**.

| Condition | Required target response |
| --- | --- |
| Missing/corrupt status file, invalid fields or unavailable saved station | Explicit setup/restore-unavailable status; do not claim all three settings restored. First-use fallback remains open; never silently default to NPO Radio 2 or another station. |
| Status-file save failure | Expose unsaved state/error, preserve the last durable snapshot and avoid claiming reboot persistence succeeded. |
| No Wi-Fi, DNS/stream failure, unsupported stream | Connecting/retrying/offline or explicit stream error; bounded attempts/backoff, keep chosen station identity, no automatic alternate station. |
| Connection later succeeds | Replace status with the selected mode using current valid data; do not repeat artwork or enable stopped output. |
| Missing title/format/bitrate | Show actual available station/status or unavailable value; do not fabricate song, codec or playback state. |
| Link loss, stream generation change or no fresh audio | Clear/invalidate meters and playing indicators; stale bars are not current audio. Reject late metadata, PCM and telemetry from older station/session generations, preserving existing local generation guards across the split migration. |
| Gate missing/lost, initialization failure or Stop | Stay/return stopped, cancel pending frames, report fault outside inhibited physical output. Network retry must never restart the scan. |
| DISPLAY/CHECK, DIM or preset during startup | Preserve their existing authorization/ownership constraints; do not repurpose these as unannounced skip buttons. Deliberate user actions supersede conflicting overlay work. |

Artwork must remain interruptible by valid display stop/safety actions.
The optional convenience skip/cancel gesture is still open; do not consume
DISPLAY/CHECK presses, alter DIM long-press semantics, or start output on a
preset press to implement it.

## Retiring manual hardware-ready confirmation: investigation plan

The goal is to remove the **manual UI authorization**, not electrical
protection. Today's software cannot sense Sony power or display `/OE`;
AHCT-v2 pin selection and ESP USB power say nothing about Sony readiness.
An unverified POWER button event is neither power-good nor an interlock.
Original-controller isolation and electrical/current certification are still
prerequisites, not conditions established by the animation.

### Proposed automatic protection, not a wiring prescription

Investigate a hardware supervisor/sequencer and independent enable gate.
Conceptually, permission requires verified input-power conditions and
blanking/scan protection, reset/fault state clear, original-driver isolation
established and physical disable released. Which rails need sensing depends
on the verified circuit and timing; monitoring every VFD rail is not assumed
mandatory. A software-ready request may permit output only within independent
hardware permission; it cannot override it.
Consider an independent watchdog if loss of scan/software service can leave
unsafe sustained outputs. Failures must inhibit/blank without depending on
the controller servicing an interrupt.

Investigate sensing actual Sony logic/TD driver supplies at suitable verified
points, and determine from the schematic whether VFD drive/filament rails need
monitoring or whether independently enforced blanking and a qualified scan
handoff can cover their power transitions. Do not equate front-button state,
standby indication, ESP power or a network response with these rails. Candidate
supervisor, comparator, isolated sense, buffer supply-switching or partial-power-tolerant
interface options all require schematic, exact part-datasheet and rail
confirmation. This chapter assigns **no** sensing GPIO, voltage threshold,
polarity, resistor size or voltage-tolerance guarantee.

Each display AHCT `/OE` is active LOW; design disabled defaults through
power-up, reset and power-down independently of firmware. **An OE pull-up
alone does not establish protection in every partial-power state.** Check
where it is powered, output high-impedance behavior with missing buffer VCC,
input/output injection paths, Sony-off backfeed through enabled buffers and
all other connected signal paths. Determine whether supply switching,
different buffering or isolation is needed; do not assume an unpowered
buffer is safe. Disabled high-impedance outputs do not guarantee safe TD
input levels: validate required bias/blanking and reset transients.

### Preferred enable direction: default disabled, qualified active enable

The user's proposed direction is active-LOW buffer `/OE` default HIGH, with
the ESP requesting LOW only after boot readiness. Treat that as a **resistor
pull-up plus a controlled pull-down**, never a hard connection to a supply
rail that an output then drives against. Boot completion alone is not the
qualified hardware permission described above.

A proposed pull-up to 3.3 V requires the exact AHCT part's `/OE` input-HIGH
specification to be checked at its actual buffer supply and operating
conditions; do not assume compatibility. It also loses its source when the
ESP's 3.3 V rail disappears while buffer 5 V remains. That partial-power
state must not accidentally enable the buffers.

The more robust **proposed** default to investigate is a pull-up referenced
to the buffer's own supply, with a suitable transistor/open-drain,
level-isolated control path that permits a qualified enable request and
lets the independent gate veto it. Never expose an ESP GPIO to 5 V, including
through a pull-up on a directly shared `/OE` net; “open-drain” alone does not
provide voltage isolation. Component choice, resistor values, connections
and control pins require schematic/datasheet validation. This is a preferred
design direction, not verified circuitry or a wiring instruction.

### Input safety versus emission safety: candidate forced-blank handoff

These are separate questions, not a requirement that every supply be present
before any serial signal can exist:

- **Input electrical/backfeed safety:** with actual TD logic powered, suitable
  referenced grounds and confirmed input ratings/timing, 5 V CLK/DATA/STB are
  not inherently problematic merely because the nominal -30 V display rail
  is absent. This is conditional on the actual schematic, parts and power
  paths; it is not permission to drive unpowered TD inputs.
- **VFD emission/scan safety:** an absent display supply generally prevents
  normal emission at that moment, but says nothing about the latched pattern
  or grid duty when it returns. Reset/high-impedance ESP outputs may leave
  arbitrary latched grid/segment data. Disabling `/OE` leaves buffer outputs
  high-impedance; it does **not** force the external CLEAR signal LOW.

Chapter 13 establishes that **external CLEAR LOW blanks** the grid outputs.
Investigate whether an independent hardware-forced blank through boot/reset,
brownout, fault and physical Stop can protect the display, with a qualified
handoff only after input power is valid and the scan/timing path is ready.
The proposed order is: establish actual target-side CLEAR LOW and defined
other signal states; enable the buffers under that enforced blank; transmit
a known blank/latch initialization to the TD devices; then allow controlled
scan illumination only after qualification. Initializing MCU RAM while
`/OE` is disabled does **not** clear the TD shift registers or output latches.
Target-side CLEAR must independently default LOW through the relevant
disabled-buffer states, using a circuit verified for those power conditions.

The user reports a **10 kΩ CLEAR-to-ground pull-down**, but may have desoldered
it: its current physical presence and location are **pending confirmation**.
This is a report of previously fitted hardware, not a newly proposed component
or an electrically verified measurement; do not add a duplicate resistor on
the basis of this chapter.
Confirm that it is physically on the **TD-side CLEAR net, downstream of the
tri-state buffer**. An ESP-side pull-down cannot hold the target net LOW
through a disabled buffer. With proposed default-HIGH `/OE`, that target-side
pull-down is intended to provide the boot/reset blank state. Verify its
actual location, measured target-side CLEAR level and power/reset sequencing
before calling that state validated. A passive pull-down while the buffer is
disabled also does not by itself prove fault blanking while an enabled output
drives CLEAR.

Hardware must be able to reassert blanking without software cooperation;
neither a one-time software “ready” flag nor a released high-impedance output
demonstrates that capability.
The forcing/arbitration circuit must avoid opposing driven outputs; no
particular circuit or new pin assignment is authorized here.

This is an **unverified, schematic- and timing-dependent alternative** to
requiring every VFD rail to be monitored. Establish whether CLEAR remains
effective throughout relevant partial-power states and whether it preserves
protection during handoff, scan stall, stop/restart and display-rail return.
Measure rail restoration both while scanning and while inhibited, including
the first unblanked grid interval and latched content. If those cases cannot
be bounded safely by the blanking design, add the necessary rail supervision
or other independent protection. Neither missing -30 V nor powered TD logic
alone proves the complete startup path safe.

Original IC701 display outputs must remain physically isolated, including
both STB routes, or use a separately verified contention-free ownership
design. Automatic permission cannot infer isolation from UART readiness.
Retain an accessible physical stop/disable independent of web UI and CPU,
and verify that it dominates software start requests.

Display and audio need separate readiness analyses/gates: display-safe
does not authorize audio chip 4, whose shared enable, mute/relay levels and
power sequencing remain unresolved. Do not parallel USB VBUS and Sony
power without verified isolation/ORing; all power paths belong in the review.

### Shared external 5 V supply clarification

The user reports an available **external 5 V / 4 A supply** intended to feed
the AHCT/level-shifter supplies and the development boards' **5 V inputs**.
This does **not** mean feeding Sony TD logic from an ESP GPIO or its 3.3 V
output. The stated capacity is not a measured system load, verified spare
current margin or safety certification.

A shared supply is a plausible design option and may simplify common
power-up, but it does not prove that the Sony TD circuitry is powered whenever
the ESPs/buffers are. Determine whether this external supply is always on or
switched in coordination with actual Sony TD power. Verify each board's 5 V
input/regulator path and USB backfeed/isolation behavior; shared ground,
distribution voltage drop, wiring/current capacity and local decoupling;
enable fail-safe/gating; and every relevant partial-power state. Shifter
supply readiness is **not** Sony logic/TD readiness. Include combined ESP
startup/radio demand and the proposed full-snow load in the measured power
budget rather than inferring headroom from the 4 A rating.

### Staged verification and exit criteria

No hardware actions are performed by this documentation change. The following
is a future qualified bench-validation plan, not instructions to rewire live
equipment. Retain chapter 13's stop, hardware disable, Sony-off and signal
disconnection precautions for reset/programming/rewiring.

| Stage | Investigation/evidence needed | Exit criterion |
| --- | --- | --- |
| 1. Establish power/ownership model | Trace actual Sony logic/driver and relevant VFD supplies, standby behavior, ESP/buffer/USB paths, original-driver disconnection and exact buffer parts. Measure rail ramps/decay and idle/reset pin behavior with appropriate equipment. | Reviewed schematic and power-state table identify every path that could drive or backfeed a powered-off device; sensing points and unresolved limits recorded. Manual confirmation stays. |
| 2. Design independent protection | Select necessary rail supervision, independent forced blanking and qualified scan-ready handoff, stable-ready qualification, fault response, safe `/OE` defaults, TD input bias, physical disable and any watchdog. Check datasheet partial-power/injection behavior for all connected parts. | Reviewed circuit justifies which rails require sensing and which transitions are protected by blanking; defines permission/fault truth tables and measured/datasheet-based limits; software cannot force enable against a fault. Manual confirmation stays. |
| 3. Verify abnormal power states | Controlled tests of Sony-only, ESP/USB-only, buffer-only and relevant partial rails; different power orders; controller/audio reset, brownout, abrupt loss, crash/stalled scan, missing sense and link loss. Include slow ramps, display-rail loss/return with TD logic still powered, forced-blank handoff, stop/restart, repeated starts and physical disable during queued output. | Captured enable/CLEAR/signal/rail behavior demonstrates required inhibition, bounded first unblanked grid duty and known latched content, no contention/backfeed outside confirmed limits, and independent physical disable. Define recovery without reset loops or unintended restart. Manual confirmation stays. |
| 4. Validate display load | Measure all-matrix snow/full-fill aggregate current, supplies, thermal behavior and scan timing at candidate brightness/duty limits, not only text or one held grid. Test worst-case queued/stalled content and Stop behavior. | Documented applicable current/thermal/electrical margins and enforced startup brightness/duty guards; existing scan/blanking protections retained. If full fill fails, do not enable it. |
| 5. Integrate and retire UI confirmation | Test authorization epochs, saved OFF, missing settings, separate audio readiness, nonblocking deadlines, stale generations, fault reporting and no restart from retries/page reloads. Preserve manual stop and service/diagnostic restrictions. | Reviewed evidence for stages 1-4 plus software tests and measured integrated power-cycle/fault results supports a specifically versioned automatic-start release. Only then replace the manual checkbox with monitored interlock status. |

The final release must document which faults are detected, reaction limits,
remaining assumptions and requalification triggers (wiring, buffers, supplies,
firmware timing or load changes). Lack of a sensor reading, failed self-test
or an unsupported hardware revision must fail closed, not waive confirmation.
Even after normal power-on becomes automatic, do not imply that connected
flashing/reset/service is safe without a separately verified procedure.

## Plain-language user walkthrough

### Today

The startup animation is a proposal, not something already installed. An ESP
reboot currently leaves the display stopped; radio normally starts at NPO
Radio 2 rather than restoring your last station. Do not remove the
hardware-ready check or repeatedly reset the connected ESP to try the effect.
Follow the [current operating guide](13-screen-driver-operating-guide.md)
for hardware preparation and authorized DISPLAY/CHECK use. Those controls
do not sense Sony power or buffer readiness.

### Intended experience after verification and implementation

1. Power the unit on normally. The two ESPs may finish booting at different
   times; the display waits for verified hardware permission.
2. Snow falls across the entire matrix area, fills it and falls away to show
   **SONY DAR-1000ES**. In the browser preview, the startup soundtrack plays
   under the animation; hardware playback is not yet approved.
3. The title gives way to sparkling dots. **E** and **S** appear first, then
   the other letters fill in **Elevated Standard**. After a short hold, the
   phrase dissolves back into dots and the display clears completely.
4. Your last radio station, brightness and display mode are restored from the
   status file; playback does not reset to the hardcoded NPO Radio 2 station.
   If connection is not ready, you see an honest connecting/retrying/offline
   message first, not an old song presented as playing.
5. A stopped display stays stopped after a stop action; connection retries
   do not turn it back on. Saved OFF will be honored; whether it suppresses
   the one-time startup artwork is still awaiting a decision.

If hardware permission fails, the physical display remains off and diagnostics
must explain why through an available interface. Do not bypass the protection.
Exact sound timing, level and any optional skip gesture are not yet finalized.

## Open decisions

- Hardware sensing points, rail limits, independent gate/sequencer, physical
  disable, watchdog need and reset/recovery policy; evidence for partial-power
  protection and original-driver isolation; whether verified forced blanking
  and scan-ready handoff can cover VFD rail transitions without sensing each rail.
- Validated full-snow brightness/duty/current limits; normal-display brightness
  persistence itself is required.
- Saved OFF versus the one-time artwork; first-use/invalid-record behavior
  and restoration after preset edits/deletion.
- Snow and star density, exact timing/dwell and optional skip input without changing
  existing DISPLAY/CHECK, DIM, preset or Stop semantics.
- When audible station playback begins; startup-sound notes, timbre, exact
  timing/mix, asset format, level and mute preference; future READY protocol
  and retry budgets.
- Country-filter persistence and broader radio/AirPlay source restoration.
