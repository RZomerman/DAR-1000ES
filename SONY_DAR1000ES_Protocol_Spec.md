# Sony DAR-1000ES Internal Parallel Bus & Handshake Protocol Proposal (Unvalidated)

> **Status: UNVALIDATED.** This is a working hypothesis, not a verified reverse-engineered protocol specification. The signal names alone do not establish polarity, direction, timing, arbitration, message contents, or the meaning of `NCK`. The sequences and hardware suggestions below have not been validated on a DAR-1000ES and must not be treated as implementation-ready instructions.

## Option 1: Retain the Original Controls

The first approach to investigate is communication over `DBS0-7`, `ACK`, `NCK`, `STB`, and `REQ` between the System Controller MCU and the Display/Front-Panel Key Controller MCU (IC701). The goal is to determine whether we can reuse the original display/front-panel controller and default controls for the internet-radio conversion, rather than going straight to the two separate display-driver chips.

Direct control of IC702 and IC703 is **Option 2, the fallback**, described in [DAR-1000ES Display Control](02-display-control.md). Neither successful bus emulation nor compatibility with the desired internet-radio interface has been demonstrated.

### Validation Required Before Implementation

- Confirm signal connections, driver ownership, voltage levels, and input thresholds from the schematic, component datasheets, and measurements.
- Passively capture all eight data lines and four control lines during startup, display changes, and individual front-panel actions.
- Establish idle levels, sampling edges, transfer direction, bus turnaround, and the actual roles of `REQ`, `ACK`, and `NCK`.
- Decode message framing and command/key payloads; do not assume raw display matrices or key codes.
- Verify level shifting, output-enable behavior, and isolation from any original controller being replaced before actively driving the bus.

The remaining sections preserve the proposed model for investigation. All specific electrical values, handshake sequences, pin assignments, and hardware choices below are unverified assumptions, not measured facts.

---

## 1. Interface Architecture Overview

The proposed model assumes an 8-bit bidirectional data bus and four asynchronous handshaking lines, using a hardware-interlocked sequence to negotiate data ownership and avoid bus contention. This model and the controllers' timing relationship require validation.

### Proposed Signal Definitions (Unvalidated)

| Signal Name | Mnemonic | Hardware Direction | Native Voltage | Protocol Description |
| :--- | :--- | :--- | :--- | :--- |
| **Display Bus Signals 0-7** | `DBS0` to `DBS7` | Dynamic Bidirectional | 5V TTL | 8-bit parallel payload data bus. Handles display character matrices, VFD grid layout streams, and raw button matrices. |
| **Request** | `REQ` | **Fixed Inbound** (Display &rarr; System) | 5V TTL | Active-Low alert line asserted by the Display Controller to flag that a front-panel button or rotary encoder action occurred and a payload byte is pending retrieval. |
| **Strobe** | `STB` | **Dynamic Alternating** | 5V TTL | Data validation clock line. Driven exclusively by the current **transmitter** to signal that the parallel data rails have settled and are safe for the receiver to sample. |
| **Acknowledge** | `ACK` | **Dynamic Alternating** | 5V TTL | Active-Low positive hardware handshake loop. Driven exclusively by the current **receiver** to confirm that the byte on `DBS0-7` has been latched into its buffer. |
| **Non-Acknowledge** | `NCK` | **Dynamic Alternating** | 5V TTL | Active-Low negative hardware handshake loop. Asserted by the receiver to declare a temporary buffer stall or busy constraint (e.g., during critical VFD raster refreshes). |

---

## 2. Bidirectional State Machinery

The hypothesis below assumes that `REQ` controls arbitration between **Write Mode** (System updating the display) and **Read Mode** (System receiving user interactions). Actual direction control and transaction initiation are unknown.

### Mode A: Write Operation (System Controller &rarr; Display VFD)
*Default idle state of the machine when no buttons are pressed.*

```
System Controller (Master)                     Display Controller (Slave)
          |                                                 |
          |--- 1. Set DBS0-7 Data Rails ------------------->|
          |--- 2. Pull STB Low (Data Valid) --------------->|
          |                                                 | [Latches Bus Byte]
          |<-- 3. Assert ACK Low (Success) -----------------| (OR NCK if Busy)
          |                                                 |
          |--- 4. Release STB High ------------------------>|
          |<-- 5. Release ACK High -------------------------|
          v                                                 v
```

1. **System Controller** verifies `REQ` is inactive (High), configures `DBS0-7` and `STB` as outputs, and places the layout byte on the bus.
2. After a physical settlement delay, the System Controller pulls `STB` **Low**.
3. **Display Controller** detects `STB` Low, samples the data bus registers, and pulls `ACK` **Low** to signify successful digestion. If the VFD grid pipeline is currently locked refreshing hardware frames, it pulls `NCK` **Low** instead, forcing the System Controller to clear the bus and schedule a retry.
4. System Controller monitors the active handshake response, releases `STB` **High**, and clears its data registers.
5. Display Controller completes the boundary cycle by releasing `ACK` (or `NCK`) **High**.

### Mode B: Read Operation (Display VFD &rarr; System Controller)
*Triggered asynchronously whenever a user interfaces with the front chassis panel.*

```
Display Controller (Transmitter)               System Controller (Receiver)
          |                                                 |
          |--- 1. Clamp REQ Low (Key Ready) ---------------->|
          |                                                 | [Swaps Pin Directions]
          |--- 2. Set DBS0-7 Data Rails ------------------->|
          |--- 3. Pull STB Low (Data Valid) --------------->|
          |                                                 | [Reads Key Register]
          |<-- 4. Assert ACK Low (Success) -----------------|
          |                                                 |
          |--- 5. Release STB High ------------------------>|
          |<-- 6. Release ACK High -------------------------|
          |                                                 | [Reverts to Write Mode]
          v                                                 v
```

1. **Display Controller** intercepts an internal keyboard matrix shift and clamps `REQ` **Low**.
2. **System Controller** detects `REQ` falling via an edge-triggered hardware interrupt (ISR), drops its own data bus lines into High-Z input mode, and switches its handshake direction configuration to prepare for incoming lines.
3. Display Controller steps up as the data transmitter: places the keyboard matrix code byte onto `DBS0-7` and triggers `STB` **Low**.
4. System Controller samples the incoming parallel word and pulls `ACK` **Low** to fulfill the boundary transaction.
5. Display Controller shifts `STB` **High** and yields the parallel bus layout.
6. System Controller drops `ACK` **High** and safely processes the captured key code, before resetting the bus structure to default write-ready properties.

---

## 3. Proposed Modernization Implementation (ESP32, Unvalidated)

An ESP32 uses 3.3V GPIO and must not be exposed to incompatible signal levels. The blueprint below assumes a 5V Sony interface and proposes **SN74LVC8T245** (8-bit bus) and **SN74LVC1T45** (1-bit transceivers). Confirm actual bus voltages and the exact parts' supply ranges, input tolerance, direction control, and isolation capabilities before selecting hardware.

The GPIO assignments below reflect the selected wiring, not a validated protocol implementation. IO17 controls ACK/NCK direction, while STB uses the inverse direction. Boot/reset isolation and electrical compatibility still require hardware validation.

### ESP32 GPIO Assignments

| Signal | ESP32 GPIO | Function |
| :--- | :--- | :--- |
| ACK | IO6 | Acknowledge signal |
| STB | IO5 | Strobe signal |
| REQ | IO7 | Request signal; proposed Sony-to-ESP input |
| NCK | IO15 | NCK signal; protocol meaning remains unvalidated |
| Handshake DIR | IO17 | HIGH: ACK/NCK ESP-to-Sony, STB Sony-to-ESP; LOW: reverse |
| BUS_OE | IO42 | Active-low output enable on the 8-bit transceiver |
| BUS_DIR | IO41 | Direction control on the 8-bit transceiver |
| DBS0 | IO40 | Data bit 0 |
| DBS1 | IO39 | Data bit 1 |
| DBS2 | IO38 | Data bit 2 |
| DBS3 | IO37 | Data bit 3 |
| DBS4 | IO36 | Data bit 4 |
| DBS5 | IO35 | Data bit 5 |
| DBS6 | IO48 | Data bit 6 |
| DBS7 | IO47 | Data bit 7 |

### Level-Shifter Hardware Interface Blueprint

```
                      +-------------------+
                      |   Sony 5V Rails   |
                      +-------------------+
                       |   |   |   |     |
        DBS0-7 Data ---+   |   |   |     |
        REQ Alert ---------+   |   |     |
        STB Handshake ---------+   |     |
        ACK Handshake -------------+     |
        NCK Handshake -------------------+
                               |
                               v
               +-------------------------------+
               | Voltage Level Shifter Network |
               +-------------------------------+
                               |
            +------------------+------------------+
            |                  |                  |
            v                  v                  v
    [SN74LVC8T245]       [SN74LVC1T45]     3x [SN74LVC1T45]
     (Data Bus)           (REQ Line)        (STB/ACK/NCK)
      DIR & OE              Fixed DIR       DIR / inverted DIR
         |                     |                  |
         v                     v                  v
     +-------+             +-------+          +-------+
     | IO41  |             |  IO7  |          | IO17  |
     | IO42  |             |       |          |       |
     +-------+             +-------+          +-------+
         |                     |                  |
         +-----------------+   |   +--------------+
                           |   |   |
                           v   v   v
                      +-------------------+
                      |  ESP32 3.3V GPIO  |
                      +-------------------+
```

The data signals use IO40, IO39, IO38, IO37, IO36, IO35, IO48, and IO47 in DBS0-to-DBS7 order. ACK, STB, and NCK connect to IO6, IO5, and IO15 respectively. IO17 drives the ACK/NCK transceiver direction controls and the inverse drives STB direction; it does not invert the STB data signal.

### Proposed Directional State Matrix (Not Safety-Validated)

Use **Break-Before-Make** sequencing: disable the 8-bit transceiver with IO42 HIGH before changing IO41 or the ESP32 data-pin modes, then enable it only after directions and data are stable. IO42 isolates only the 8-bit data bus, not ACK/STB/NCK. Safe handshake turnaround and reset behavior need separate validation.

| Target Action State | BUS_OE (IO42) | BUS_DIR (IO41) | Handshake DIR (IO17) | STB Direction | ACK/NCK Direction | ESP32 Data Pins |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **System Boot / Reset Initialization** | **HIGH** required to isolate data bus | Set before enabling | Establish before driving; reset isolation unvalidated | Unvalidated at reset | Unvalidated at reset | `INPUT` (High-Z) |
| **VFD Display Update Mode** | **LOW** (Connected) | **HIGH** (Outbound, assumed) | **LOW** | ESP-to-Sony | Sony-to-ESP | `OUTPUT` |
| **Keyboard Ingestion Mode** | **LOW** (Connected) | **LOW** (Inbound, assumed) | **HIGH** | Sony-to-ESP | ESP-to-Sony | `INPUT` |

IO41 HIGH=outbound and LOW=inbound retain the proposed polarity and must be checked against the 8-bit transceiver's A/B wiring. IO17 polarity is the specified wiring convention above; the Sony transaction sequence remains unvalidated. A GPIO being an input at reset does not itself establish a safe level on OE or DIR; verify hardware biasing and startup behavior.

---
*Unvalidated investigation proposal for open-source hardware modernization. No working protocol implementation or hardware validation is claimed.*