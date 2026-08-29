# Sony DAR-1000ES ESP32 Development Board

## Overview

This development board is designed as the primary hardware platform for the **Sony DAR-1000ES modernization project**. It enables rapid experimentation with multiple integration strategies while preserving the original Sony hardware wherever possible.

The board incorporates **three ESP32-S3 controllers**, although only two are expected to be required for the final solution. The third controller is included specifically as a contingency path for direct display control if communication through the original Sony system controller (IC701) proves impractical.

The objective of this board is to validate all critical hardware interfaces before committing to a final production design.

---

## Design Goals

The board supports testing of four major functional areas:

### 1. Digital Audio Injection

A dedicated Audio ESP is responsible for generating digital audio and feeding it directly into the Sony audio processing chain.

Target interface:

| Signal | Destination |
|----------|----------|
| ATT/SHIFT/LATCH | IC501 |
| LRCKI | IC501 |
| DATAI | IC501 |
| BCKI | IC501 |

This allows the DAR-1000ES to receive modern digital audio streams while continuing to use the original Sony DAC and audio circuitry.

---

### 2. IC701 Controller Bus Injection

The primary Controller ESP can emulate and inject data directly onto the internal Sony communication bus.

Supported signals:

| Signal |
|----------|
| DBS0-DBS7 |
| ACK |
| STB |
| REQ |
| NCK |

The intention is to determine whether the original firmware architecture can be leveraged by presenting the system with data equivalent to what IC701 would normally generate.

If successful, this becomes the preferred approach because it preserves most of the original Sony subsystem interactions.

---

### 2b. Direct Sony Hardware Control

In addition to bus injection, the Controller ESP can directly control critical DAR-1000ES hardware signals.

Supported outputs include:

| Function |
|----------|
| D.MUTE |
| A.MUTE |
| Audio Relay |
| Power Control |
| Reset Control |

This approach allows complete control over the audio subsystem even if communication through the original system controller becomes problematic.

---

### 2c. Keypad Matrix Interface

The board provides direct access to the original Sony button matrix.

Capabilities:

- Scan front-panel buttons
- Decode keypad presses
- Replace original controller functionality
- Support future custom user interface options

The keypad matrix is interfaced through a dedicated keypad controller, allowing reliable operation without loading the original circuitry.

---

### 3. Direct Display Driver Control (Fallback Strategy)

A third ESP32-S3 is included exclusively as a backup solution.

If direct IC701 bus injection does not provide sufficient control of the front-panel display, this ESP can take over the display subsystem directly.

Target functionality includes:

- Direct control of display driver ICs
- Row driving
- Column driving
- Segment updates
- Custom display rendering

This completely bypasses the original display control architecture and provides maximum flexibility for future software development.

---

## Controller Architecture

### Audio ESP

Primary responsibility:

- Streaming audio reception
- Audio decoding
- I²S generation
- Interface to IC501

Interfaces:

- Audio output signals
- UART communication with Controller ESP

---

### Controller ESP

Primary responsibility:

- System orchestration
- IC701 bus injection
- Sony hardware control
- Keypad processing

Interfaces:

- Bus emulation
- Control outputs
- Keypad matrix
- UART communication

---

### Display ESP

Primary responsibility:

- Display subsystem experimentation
- Direct display driver control
- Fallback display architecture

Interfaces:

- Row control
- Column control
- UART communication

---

## ESP-to-ESP Communication

The three ESP32 controllers can communicate through dedicated UART links.

This architecture allows:

- Separation of audio processing from system control
- Reduced software complexity
- Independent firmware development
- Easier debugging and validation

The Audio ESP and Display ESP act as peripherals while the Controller ESP functions as the primary coordinator.

---

## Power Architecture

Although each ESP controller has its own USB-C connector, USB is intended primarily for:

- Firmware upload
- Debugging
- Serial console access

Normal operating power is supplied directly from the Sony DAR-1000ES.

Features include:

- Independent ESP enable/disable jumpers
- Selective controller isolation
- Simplified troubleshooting
- Reduced power consumption during testing

This allows any controller to be disabled while maintaining operation of the remaining subsystems.

---

## PCB Design Philosophy

To minimize manufacturing cost and simplify assembly, the PCB has been intentionally designed as a:

**Single-layer board**

Characteristics:

- Single copper layer
- Limited number of wire bridges
- Large ground areas
- Easy inspection and rework
- Low fabrication cost

The use of a mostly single-sided layout also makes future modifications straightforward during the experimental phase of the project.

---

## Development Strategy

The board is intended to validate three possible integration levels:

### Level 1 – Audio Only

- Audio ESP operational
- Original Sony control system retained

### Level 2 – IC701 Emulation

- Controller ESP injects Sony bus traffic
- Original display drivers retained
- Original Sony architecture largely preserved

### Level 3 – Full Replacement

- Controller ESP replaces IC701 functionality
- Display ESP directly drives display hardware
- Complete modern control stack running on ESP32 controllers

---

## Current Status

This development board serves as the primary validation platform for the DAR-1000ES retrofit effort.

The modular three-ESP architecture provides multiple fallback paths, ensuring that all major subsystems can be independently evaluated before a final production design is selected.
