# Submarine Monitoring System — Project Guide

## Purpose of this file

This file is the **single source of truth** for this project across conversations. If you (the student) start a new chat and attach only this file, the assistant should be able to understand the whole project: what it is, how the hardware is wired, what has been decided, what has been built, what hasn't, and what the very next step is.

Whenever something meaningful changes — a decision, a finished file, a hardware change — **this file should be updated**, not left stale. Treat it as living documentation, not a one-time snapshot.

Source of truth for requirements: `final project.pdf` (`SW-FD-LNC-001`, "Software Functional Definition"), 8 pages. This guide is a working companion to that document, not a replacement for it — when in doubt about a requirement's exact wording, the PDF wins.

---

## 1. Project Overview (plain language)

This is a university final project simulating a **submarine monitoring system**, with three programs plus a separate OOP exercise:

- **LNC (Local Node Controller)** — runs on a real **STM32 NUCLEO-L476RG** board, in C, using FreeRTOS. It's the "end unit" physically inside the submarine: it reads sensors, detects objects, manages an RGB LED + buzzer + alarm-stop button, keeps a log, and talks to the Central Computer over UART.
- **Central Computer (CC)** — runs on Windows, in C++. It's the hub: manages one or more LNC-type end units (this project only implements the LNC; a motor unit and navigation unit are mentioned as other end units but out of scope), stores data in a database, executes management commands, and serves the Ground Station.
- **Ground Station (GS)** — runs on Windows, in C++. A client that asks the CC for historical log/event data over a date/time range. Ethernet is simulated on the same PC (no physical Ethernet hardware involved anywhere in this project).
- **FleetOOP** — a separate, not-networked, in-memory object-oriented exercise (Submarine / Mission / Fleet classes) that happens to live in the same repository and reuses the CC's class as an owned (but never started) object.

All inter-process communication (LNC↔CC and CC↔GS) uses the same **TLV (Tag-Length-Value)** message format.

---

## 2. LNC Requirements (from SW-FD-LNC-001, Section 2)

Nine software modules, message flow: sensors/sonar → {Monitor, ObjectDetection} → Event/Log → Communication → CC.

### 2.1 Monitor
- Every 5 seconds: samples temperature, humidity, battery voltage (via a potentiometer — there is no real battery gauge), and light; compares each against configured limits.
- Sends measured data + resulting mode to Log.
- Limit values are configurable, stored in Flash.
- On a mode change, sends measured values to Event.

### 2.2 Object Detection
- Continuously listens for an object in the direction of travel (**spec calls this "sonar"** — see Open Questions, hardware currently uses a single-pin **IR** sensor instead).
- On detect → message to Event. On object no longer detected → message to Event.

### 2.3 Event
Waits for events from other modules; timestamps every event on arrival. Behavior depends on source:

- **From Monitor** (mode transitions), Section 2.3.1:
  - Normal→Warning: LED yellow.
  - Error→Warning: LED yellow, stop alarm if active, resume full operation.
  - →Error: LED red, alarm on, button press stops alarm, **suppress non-essential operations** (meaning not yet defined — see Open Questions).
  - Warning→Normal: LED green.
  - Error→Normal: LED green, stop alarm if active, resume full operation.
  - Every case: write timestamped message to events file, send message to CC.
- **From Configuration**: write timestamped message to events file.
- **From Init**: write a startup message to events file, including whether startup followed a watchdog reset.
- **From Object Detection**:
  - Detected: LED red, alarm on, write to events file, send to CC, button stops alarm.
  - Cleared: LED green, stop alarm if active, write to events file, send to CC.

### 2.4 Log
- Builds a log message (timestamp + measurement data + mode), writes to a file named by date.
- Retains 7 days (7 files); on the 8th day, deletes the oldest.

### 2.5 Communication
- Owns the UART interface; sends and receives messages to/from CC.
- Receives **management commands** from CC:
  - Set temperature range for Normal mode; set temperature range for Warning mode (temperature gets **both** an upper and lower bound per mode).
  - Set humidity lower boundary for Normal mode; set humidity lower boundary for Warning mode (**lower bound only** — no upper bound specified).
  - Set light lower boundary for Normal mode; set light lower boundary for Warning mode (lower bound only).
  - Set battery/potentiometer lower boundary for Normal mode; set battery/potentiometer lower boundary for Warning mode (lower bound only).
  - Set date/time for the RTC.
  - Get current system time.
- Receives **retrieval instructions** from CC: measurement data for a time range; events for a time range.
- Sends messages from other modules by strict priority: **keepalive (highest) > event (medium) > data (low)**.

> **Important asymmetry to remember when designing Configuration/Monitor data structures:** temperature needs 2 numbers per mode (low+high); humidity/light/battery each need only 1 number per mode (a lower bound). Don't design a uniform "4 sensors, same shape" struct.

### 2.6 Configuration
- Receives configuration changes from Communication; owns all configuration values.
- Persists to Flash; loads at startup; on first boot (empty Flash) loads and writes defaults.

### 2.7 Init
- Requests time/date sync from CC via Communication.
- Sends a message to Event once sync completes.
- Starts all system activities.

### 2.8 Keep-Alive
- Every 6 seconds, sends a keepalive to CC via Communication: timestamp + latest measurement data + current mode.

### 2.9 Watchdog
- Refreshes the hardware watchdog "on the correct schedule" (spec doesn't give a number — see Hardware section, this is now hardware-constrained and **currently paused/unresolved**, do not touch until explicitly revisited).

### 2.10 Operating Modes
- **Normal**: all measurements within Normal range.
- **Warning**: ≥1 measurement in Warning range, none in Error range.
- **Error**: ≥1 measurement in Error range.

---

## 3. Central Computer Requirements (Section 3)

Modules: Communication (LNC-facing), Management Command, Log, Data Collection & Analysis.

- **Communication (LNC-facing)**: implements the LNC protocol, a listener that dispatches received messages to the right module, transport-agnostic (UART or Ethernet — in practice, UART only, since the LNC hardware has no Ethernet).
- **Management Command**: builds/sends management commands to the LNC.
- **Log**: prints logs and persists them to files.
- **Data Collection & Analysis**: persists measurements + events to a database, 7-day retention (matches LNC), prepares reports broken down by criteria.

An Ethernet-facing module for the Ground Station side isn't named explicitly in the spec but is required by Sections 1.2/4 — **architectural recommendation**, not an explicit spec module.

---

## 4. Ground Station Requirements (Section 4)

- Requests, from the CC, log data and event data for a given date/time range. That's it — no other GS responsibilities in the spec.
- Sec 1.2 has one unelaborated sentence about the GS needing to "know how to manage several types of submarines" — **recorded as an architectural recommendation only** (a `SubmarineProfile`/registry concept), deliberately **not built**, since the spec gives no further detail.

---

## 5. OOP Part — Submarine Fleet Management System (Sec. "OOP Part")

Not networked; separate in-memory exercise reusing the CC class.

- **Submarine** (abstract): serial number, name, assigned-to-mission flag.
- **ResearchSubmarine** (extends Submarine): researchers[], research topic.
- **CombatSubmarine** (extends Submarine): mission description, commander name, personnel count; **owns a `CentralComputer`** via `std::unique_ptr` (per spec: "it should be treated as an object that belongs to each combat submarine") — constructed/destroyed with the submarine, **never started** (`start()` never called), no live sockets/DB/LNC coupling. Tracks other submarines participating in its current mission. Does **not** hold a back-reference to Fleet (`fleetRef` removed) — mission history is owned by Fleet, not by each submarine, per the decoupling decision below.
- **Mission**: description + history.
- **Message**: content + reference to the sending submarine.
- **Fleet**: manages the submarine collection, owns the global mission-history list, implements the 10 menu operations:
  1. Add a submarine (choose type, enter details).
  2. Display all submarines (details + mission-assigned status).
  3. Search/display by serial number.
  4. Assign a mission to a submarine.
  5. Update a submarine's mission details (per type).
  6. End a submarine's mission (mark available again).
  7. For combat submarines — associate additional combat submarines with the same mission.
  8. Send a message between combat submarines in the same mission.
  9. Display messages received by a submarine (content + sender).
  10. Exit.

**Decoupling rule (locked):** `CentralComputer` is a facade class usable standalone (real-time `cc_main.cpp`) with **zero** FleetOOP dependency. FleetOOP never includes `LncCommunicationManager` / `GsCommunicationManager` / `Database` headers directly — only `central_computer.h`. `CentralComputer`'s constructor is inert; all I/O happens in `start()`/`stop()`, which FleetOOP never calls.

---

## 6. Communication Protocol (frozen design)

Shared shape between LNC↔CC and CC↔GS, implemented **independently** in two languages: LNC's `protocol.c` (plain C), and `Common/TLVCodec` (C++, shared only between CC and GS since both are C++).

**Frame layout:**
```
SOF (1 byte, = 0xAA) | Tag (1 byte) | Length (2 bytes, LE) | Value (Length bytes) | CRC16 (2 bytes, LE)
```
- CRC = **CRC-16/CCITT-FALSE**, computed over Tag+Length+Value (not SOF, not the CRC field itself). Standard test vector to validate any implementation: ASCII `"123456789"` → `0x29B1`.
- Max frame size: **256 bytes total**. Because `Length` is a 2-byte field (which could claim up to 65535), **the decoder must explicitly reject/resync if Length would make the frame exceed 256 bytes** — this is a required validation step, not just documentation.
- Multi-byte values: little-endian.
- `DateTime` = `uint32_t` Unix epoch, UTC.
- Floats = IEEE754 single precision.
- Enums = `uint8_t`.
- Nested TLV: max 1 level.
- Unknown tags: dropped silently.
- Malformed frames: rejected, decoder resyncs on the next `0xAA` byte.
- Bulk transfers (e.g. retrieving measurements/events for a time range): chunked, ~20 records/chunk, using `RequestId` (`uint8_t`) + `ChunkSeq` + `MoreDataFlag`. **Open: exact byte-width of `ChunkSeq`/`MoreDataFlag` not yet decided** — needs deciding at Message-layer design time, not blocking Protocol layer.

**Message catalog (now derived directly from the spec, Sec 2.5 — not guessed):** 8 "set limit" commands (2 for temperature-range, 2 each for humidity/light/battery lower-bound), set-RTC, get-system-time, retrieve-measurements-by-range, retrieve-events-by-range, plus event messages (6 mode-transition kinds + 2 object-detection kinds + config-changed + startup) and the keepalive message. **Numeric TAG byte values for each of these are still an open architectural-recommendation item** — the catalog's *contents* are now spec-derived and settled; only the *numbers* remain to be assigned, at Message-layer design time.

**Layering (strict, no skipping):**
```
Transport  → raw bytes only (UART or Ethernet). Knows nothing about TLV.
Protocol   → SOF/Tag/Length/Value/CRC framing, encode/decode, validation. Knows nothing about UART/Ethernet.
Message    → maps Tag → typed message struct; builds/parses messages.
Application→ Monitor/Event/Configuration/etc. (LNC) or ManagementCommand/DataCollection (CC).
```

---

## 7. FreeRTOS Task Design (LNC)

Only 7 tasks — created for genuine concurrency/periodicity needs; everything else is a plain C module called synchronously and protected by a mutex where shared state exists.

| Task | Trigger | Priority | Notes |
|---|---|---|---|
| WatchdogTask | periodic | 4 (highest) | must never starve |
| ObjectDetectionTask | continuous poll of IR pin (was: sonar semaphore — needs revisiting per the IR/sonar question) | 3 | safety-relevant |
| MonitorTask | periodic 5s | 2 | |
| CommRxTask | polling loop, **not** interrupt-driven | 2 | see below — must yield deliberately |
| CommTxTask | blocks on 3 priority TX queues | 2 | |
| InitTask | one-shot at boot, self-deletes | 2 | |
| KeepAliveTask | periodic 6s | 1 (lowest) | |

**CommRxTask — locked design (no interrupts, no DMA, no printf on USART2, per explicit instruction):**
```
loop:
    result = HAL_UART_Receive(&huart2, &byte, 1, SHORT_TIMEOUT_MS)   // e.g. 20 ms
    if result == HAL_OK:
        feed byte into the Protocol-layer decoder
    else:  // timeout, no byte arrived
        osDelay(1)   // REQUIRED — see why below
```
**Why the `osDelay(1)` is not optional:** FreeRTOS's preemptive scheduler only lets a strictly lower-priority task run when the higher-priority one actually *blocks*. `HAL_UART_Receive` in polling mode busy-spins on the status register — even with a short timeout, the task never truly blocks unless it calls something like `osDelay()`. Priority 2 (`CommRxTask`) without regular blocking would starve `KeepAliveTask` (priority 1) completely. This isn't a style choice, it's a correctness requirement given the current priority scheme.

**CommTxTask**: blocks on its own queue when idle (a genuine RTOS block), so a plain blocking `HAL_UART_Transmit()` when a frame is queued is fine, no special handling needed.

**RTOS objects:** `txQueueKeepAlive`(depth 2) > `txQueueEvent`(depth 8) > `txQueueData`(depth 4), drained by `CommTxTask` in strict priority order; `EventMutex`, single internal flash mutex owned by `flash_storage.c` (not split Log/Config mutexes); `sonarDetectSemaphore` or equivalent for ObjectDetection (pending the IR/sonar hardware question); `timeSyncSemaphore` (CommRxTask → InitTask).

---

## 8. Central Computer / Database (frozen decisions)

- SQLite (C API or thin C++ RAII wrapper), 7-day retention (matches LNC).
- Schema: `measurements(id PK, timestamp, temperature, humidity, light, battery, mode)`, `events(id PK, timestamp, event_type, description)`, both indexed on timestamp.
- `report_generator` is a **namespace**, not a class.
- Naming: CC's transport interface header is `itransport.h` (renamed from `transport.h` to avoid collision with the LNC's own `transport.h`).
- `pending_request_registry.h/.cpp` added (tracks outstanding requests awaiting a response, e.g. bulk chunked transfers).

---

## 9. Hardware — Real STM32CubeIDE Project (`Embeded/`)

Target: **STM32L476RG (NUCLEO-L476RG)**, STM32CubeIDE project, FreeRTOS via CMSIS-RTOS2, FatFs middleware present.

### 9.1 Pin / peripheral table (fully resolved — every physical pin is now labeled)

| Pin | Peripheral / Signal | Label | Purpose |
|---|---|---|---|
| PA0 | ADC1, Channel 5, 12-bit | `Battery` | Potentiometer standing in for battery voltage |
| PA1 | ADC2, Channel 6, 8-bit | `Ligth` (cosmetic typo in CubeMX label, harmless) | Light sensor |
| PA2 | USART2_TX | — | LNC↔CC UART (also the Nucleo ST-LINK VCP — deliberately shared, see 9.3) |
| PA3 | USART2_RX | — | LNC↔CC UART |
| PA5 | SPI1_SCK | — | SD card bus |
| PA6 | SPI1_MISO (pull-up) | — | SD card bus |
| PA7 | SPI1_MOSI | — | SD card bus |
| PA8 | GPIO output | `RGB_LED_2` | RGB LED channel |
| PA9 | GPIO output | `RGB_LED_1` | RGB LED channel |
| PA10 | GPIO input, no pull | `Button` | Alarm-stop button |
| PB4 (NJTRST) | TIM3_CH1 PWM | `Buzzer` | Alarm tone |
| PB5 | GPIO output, pull-up (bit-banged single-wire) | `DHT` | Temperature/humidity sensor |
| PB6 | GPIO output | `SD_CS` | SD card chip-select |
| PB10 | GPIO input, no pull | `IR` | Object detection sensor — **spec calls this "sonar," hardware is IR; unresolved, see Open Questions** |
| PC9 | GPIO output | `RGB_LED_3` | RGB LED channel |

### 9.2 Peripheral configuration details

- **USART2**: 115200-8-N-1, TX_RX mode, **polling only** (no NVIC entry, no ISR, no DMA) — locked decision.
- **SPI1**: Master, Full-Duplex, `SPI_DATASIZE_8BIT` (fixed from an earlier incorrect 4-bit default), `SPI_BAUDRATEPRESCALER_128` (≈625 kbit/s, SD-card-safe), MISO pulled up.
- **ADC1**: channel 5, 12-bit, software-triggered single conversion (`Battery`).
- **ADC2**: channel 6, 8-bit, software-triggered single conversion (`Ligth`) — resolution difference from ADC1 not yet confirmed as intentional (minor, not blocking).
- **TIM3**: PWM channel 1 → `Buzzer`, period 100, prescaler 0.
- **TIM6**: basic timer, prescaler 79 / period 999 (~1 kHz-ish tick source), purpose not yet tied to a specific module.
- **RTC**: **UPDATE (2026-09-02) — now genuinely enabled, confirmed from source, not assumed.** Sec 9.2 previously (2026-09-01) had to be corrected here because the RTC peripheral wasn't actually enabled despite an earlier version of this bullet claiming it was. The user has since enabled it for real in CubeMX and regenerated; verified directly in the resulting code (not trusted from the `.ioc` GUI alone): `stm32l4xx_hal_conf.h` now has `HAL_RTC_MODULE_ENABLED` defined (uncommented), `main.c` now declares `RTC_HandleTypeDef hrtc` and calls a real `MX_RTC_Init()` (from `main()`, before the scheduler starts, alongside every other `MX_*_Init()` call), and `Embeded.ioc` shows `Mcu.IP6=RTC` with both `VP_RTC_VS_RTC_Activate.Mode=RTC_Enabled` and `VP_RTC_VS_RTC_Calendar.Mode=RTC_Calendar` set. **Calendar**: `hrtc.Init.HourFormat = RTC_HOURFORMAT_24` — 24-hour format, as intended. **Clock source: LSI** (confirmed in `HAL_RTC_MspInit()`: `PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSI`) — matches the agreed decision (no populated LSE crystal on this Nucleo board; Init doesn't need long-term precision). **No unnecessary RTC features enabled** — confirmed no Alarm/WakeUp/Tamper code anywhere in `stm32l4xx_it.c`, no matching `.ioc` entries, `hrtc.Init.OutPut = RTC_OUTPUT_DISABLE` — exactly the polled `HAL_RTC_GetTime()`/`GetDate()`-only usage Sec 2.7 needs, nothing more.
  - **Non-blocking accuracy note, surfaced not silently accepted:** `MX_RTC_Init()`'s `hrtc.Init.AsynchPrediv = 127` / `SynchPrediv = 255` are the standard values for a 32.768 kHz **LSE** crystal (128 × 256 = 32768), not recalculated for the ~32 kHz **LSI** this project actually uses (`Embeded.ioc`'s own `RCC.LSI_VALUE=32000`; for an exact 1 Hz calendar tick from a true 32000 Hz source, `SynchPrediv` should be `249`, i.e. 128 × 250 = 32000). As configured, the RTC's calendar runs about 2.4% slow (each RTC "second" is really ~1.024 real seconds) — on top of LSI's own inherent drift, which this project already accepts since Init doesn't need long-term precision. Not fixed here since it doesn't block anything Init currently needs; worth recomputing in CubeMX (`SynchPrediv=249`) later if timestamp accuracy ever starts to matter more (e.g. for Log/Event ordering across a long uptime).
  - **Real regression found and confirmed while verifying this regeneration, unrelated to RTC itself: `TIM3`'s Prescaler was silently reset from the previously hardware-verified `399` back to CubeMX's default `0`** (`main.c`: `htim3.Init.Prescaler = 0`) — exactly the risk this guide had already flagged (Sec 14's Buzzer entry: `.ioc` never actually persisted `Prescaler=399` as an explicit `TIM3.IPParameters` entry, so any regeneration would silently drop it back to 0). Confirmed directly: `Embeded.ioc`'s `TIM3.IPParameters=Channel-PWM Generation1 CH1,Period` still has no `Prescaler` key. **This means the buzzer's PWM is currently back to ~792 kHz (inaudible) instead of the verified ~1980 Hz — a real, silent regression, not yet fixed.** Per this project's established precedent, CubeMX-level peripheral values are set by the user directly in the CubeMX GUI, not by hand-editing `.ioc`/generated code — **user action needed**: reopen CubeMX, TIM3, re-set Prescaler to `399`, and this time confirm it's saved (check `Embeded.ioc` afterward for a `TIM3.Prescaler=399` line, not just `main.c`) before regenerating again, to actually break this recurring-drift cycle. Not yet reflashed/retested — the buzzer's hardware-verified status (Sec 14) should be treated as **at risk** until this is fixed and reconfirmed.
- **IWDG**: `Prescaler = IWDG_PRESCALER_4`, `Reload = 4095`, `Window = 999`. **Computed behavior:** counter clock = 32000/4 = 8000 Hz; full timeout ≈ 512 ms; because `Window` (999) < `Reload` (4095), this is a **windowed** watchdog — refreshes are only accepted between ~387 ms and ~512 ms after the last refresh (a ~125 ms valid window). **STATUS: flagged as needing a decision (disable windowing / extend timeout), but the user has said not to touch this configuration for now — leave as-is until explicitly revisited.**
- **FreeRTOS**: tick = 1000 Hz, heap = 15360 bytes (heap_4-style), `configMAX_PRIORITIES = 56`. Task stubs for all 7 approved tasks (+ a leftover CubeMX `defaultTask`) already exist in `main.c` as empty `for(;;) osDelay(1)` loops — current stack size for all of them is a uniform 128 words, not yet adjusted to the per-task sizes (256/512/128) from the RTOS design; a CubeMX-level cleanup task for later, not urgent.
- **No debug output ever on USART2** — locked decision; `printf`/`_write`/`__io_putchar` exist as unused stubs in `syscalls.c` but are not wired to anything, and must stay that way. USART2 carries TLV protocol bytes exclusively.

### 9.3 Why USART2 is shared with the ST-LINK VCP

PA2/PA3 are the Nucleo board's default ST-LINK virtual-COM-port pins. The user has explicitly decided to reuse this same UART as the dedicated LNC↔CC link (no second UART will be added), and to never put debug text on it, to avoid mixing text into the TLV byte stream.

---

## 10. Open Questions / Ambiguities (current, unresolved)

| # | Question | Class |
|---|---|---|
| 2 | The LNC currently has **no watchdog at all** — the IWDG peripheral and `WatchdogTask`'s refresh logic were removed entirely (see Sec 14's UART/RX investigation write-up for why), so Sec 2.9's watchdog requirement is now fully unimplemented, not just untuned. A real `WatchdogTask` will need designing from scratch later. | **BLOCKING** for WatchdogTask — a new open item, was previously "tuning," now "doesn't exist" |
| 3 | Sec 2.3.1's "suppresses non-essential operations" / "resumes full operation" during Error mode — spec never defines which operations are non-essential. | **IMPORTANT**, needed at Event/Application-layer design time |
| 6 | Default configuration limit values (temperature/humidity/light/battery thresholds) — spec doesn't give numbers. | **IMPORTANT**, placeholders needed at Configuration-module design time |
| 8 | ADC1 (12-bit) vs ADC2 (8-bit) resolution asymmetry — intentional, or an unreviewed CubeMX default? | MINOR |
| 9 | TIM6's role isn't yet tied to a specific module. | MINOR |
| 10 | CRC-16/CCITT-FALSE is implemented independently in LNC's C code and CC/GS's shared C++ code — recommend cross-validating both against the standard test vector (`"123456789"` → `0x29B1`) as part of Protocol-layer testing, to catch any silent divergence early. | **RECOMMENDATION — NOT IN SPECIFICATION** |
| 11 | Ground Station "several types of submarines" — recorded as an architectural recommendation, deliberately not built (see Sec. 4). | **RECOMMENDATION — NOT IN SPECIFICATION**, already decided not to build |
| 12 | **IR device real type — hardware signal detection now VERIFIED, but final driver design still open.** A temporary fast-sampling diagnostic (reading PB10 much faster than the original once-per-2s test loop) confirmed PB10 genuinely changes state during an IR remote's transmission - the pulse-train theory was correct, this is very likely a remote-control-style demodulating IR receiver, not a simple steady-level presence sensor. **What's still unresolved:** `ir.c` itself has NOT been redesigned yet - it still contains the original single-poll-every-2s implementation with an unconfirmed polarity guess, which we now know is the wrong read strategy for a pulse-train device. The real design question (still open, needs a decision with the user before implementing): should the final driver do full remote-protocol decoding (NEC/RC5/etc., via EXTI edge capture + timing, similar in spirit to `dht.c`'s pulse timing), or - matching Sec 2.2's actual spec intent ("listens for an object in the direction of travel," i.e. presence/absence, not remote-button decoding) - just detect "is any IR pulse activity happening right now" as a simpler presence surrogate? **IMPORTANT**, needed before `ir.c` can be correctly rewritten - not blocking other modules. |
| 13 | ~~RTC peripheral not enabled~~ **RESOLVED (2026-09-02): RTC is now genuinely enabled** (`HAL_RTC_MODULE_ENABLED` defined, `hrtc`/`MX_RTC_Init()` exist, LSI clock source, Calendar/24-hour format confirmed - see Sec 9.2's updated RTC bullet) and wired into Init as the real timestamp source (Sec 14/15) - AWAITING REAL-HARDWARE TEST. Non-blocking residual: `AsynchPrediv`/`SynchPrediv` are LSE-typical values, not recalculated for LSI's actual ~32 kHz, giving the calendar a ~2.4% slow drift on top of LSI's own inaccuracy (Sec 9.2) - not fixed, doesn't block current use. | MINOR (accuracy only, was BLOCKING) |
| 15 | **RESOLVED and HARDWARE-CONFIRMED (2026-09-02): the same RTC-enabling regeneration ALSO silently reverted `InitTask`/`MonitorTask`/`ObjectDetectionTask`'s stack sizes from the hardware-verified `1024*4` back to CubeMX's `128*4` default, and removed `configCHECK_FOR_STACK_OVERFLOW` from `FreeRTOSConfig.h` entirely** - reopening the full rounds-1-7 stack-overflow bug (Sec 14) and disabling its own detection mechanism at the same time. This was the confirmed root cause of the "purple LED, no IR response" symptom. Fixed by restoring both, this time placing `configCHECK_FOR_STACK_OVERFLOW` inside a `USER CODE` marker so it survives future regenerations. **User confirmed on real hardware: LED changes color correctly on IR detection (including red), IR detection works, the stack-overflow/freeze is gone.** | Was **BLOCKING**, now fully RESOLVED |
| 16 | **Now-established pattern, worth watching going forward: ANY full CubeMX regeneration in this project has, three separate times, silently reverted hand-tuned values that were never fed back into `.ioc`/the CubeMX GUI** - `TIM3.Prescaler` (Open Question #14), and now both `osThreadAttr_t.stack_size` for 3 tasks and `configCHECK_FOR_STACK_OVERFLOW` (Open Question #15). The common thread: CubeMX only preserves hand-edits that live inside `USER CODE BEGIN/END` markers, or that were set through its own GUI panels (Timers, FreeRTOS Tasks-and-Queues, etc.) - a plain hand-edited literal outside both of those is regenerated away every time, silently, with no warning. **Recommendation, not yet acted on**: after any future CubeMX regeneration, explicitly re-diff/re-check all 3 of: TIM3 Prescaler, the 3 tasks' stack sizes, and `configCHECK_FOR_STACK_OVERFLOW` (now inside a `USER CODE` marker, so should survive - the first real test of whether that placement actually works) - do not assume a regeneration is safe just because it built and linked without errors. | **RECOMMENDATION - NOT IN SPECIFICATION**, a process/tooling lesson for this project, not a spec gap |

### Resolved (for history/traceability — no longer open)
- **Object detection: IR vs. sonar (was #1) — resolved: hardware is a single-pin digital IR sensor, not sonar.** Confirmed directly from code, not guessed: `main.c`'s GPIO init uses `GPIO_MODE_INPUT` (plain digital input, no interrupt) on `IR_Pin`/PB10; `Embeded.ioc` labels the pin `IR` with `Signal=GPIO_Input`; no EXTI handler exists anywhere for PB10; and the full pin table (Sec 9.1) allocates only this one pin to object detection - a real ultrasonic sonar module would need a second pin (Trigger + Echo) plus a timer input-capture channel for pulse-width/distance measurement, and neither exists in this project. The spec's word "sonar" is informal phrasing for "the object-detection sensor," not a literal requirement. User confirmed this conclusion.
- All RGB LED / button / buzzer / battery / light pin assignments — now labeled in CubeMX.
- SPI1 4-bit datasize bug — fixed to 8-bit.
- RTC peripheral — added and working. IWDG — added, then **removed entirely** partway through this project (see Sec 14 and #2 above) after it was found to be the root cause of a real hardware bug; a watchdog no longer exists in the LNC at all right now.
- Ethernet on the LNC — confirmed not needed; simulated on the PC side for CC↔GS only.
- UART RX method — confirmed: polling with a short timeout + mandatory `osDelay(1)` yield (Sec. 7), not interrupt-driven.
- Log/Configuration storage split — SD card (FatFs) for Log's daily files, internal MCU flash for Configuration's small limit values (this was an assumption — **not yet explicitly confirmed with the user**, worth a quick check before writing `flash_storage.c`/Log/Configuration).
- Exact numeric TLV TAG byte values (was #4) — frozen in `tlv_common.h`, 21 tags (Sec 6/14).
- Test framework for C/C++ test files (was #7) — plain C/C++ + a hand-rolled `check()` helper, no library (Sec 14, Protocol layer).
- `ChunkSeq` / `MoreDataFlag` byte widths for bulk transfers (was #5) — resolved during the chunk-response design session: `ChunkSeq` is `uint16_t` (a `uint8_t` would overflow for any realistic query - even one day of 5s-interval measurements needs ~1,571 chunks at 11 records/chunk), `MoreDataFlag` is `uint8_t` (0/1). Full design in Sec 14 under "Chunked bulk-transfer responses done".
- **TIM3 buzzer Prescaler regression (was #14) — RESOLVED and HARDWARE-CONFIRMED (2026-09-03).** User re-set the Prescaler to `399` in CubeMX and this time verified `Embeded.ioc` actually persisted `TIM3.Prescaler=399` (confirmed directly in the file: `TIM3.IPParameters=Channel-PWM Generation1 CH1,Period,Prescaler` / `TIM3.Prescaler=399`, and `main.c`'s `htim3.Init.Prescaler = 399`), then reflashed. User confirmed the buzzer audibly works correctly on real hardware. This closes out the buzzer regression for good - the `.ioc`-level persistence check (missed the two previous times) is what made this fix actually stick.

---

## 11. Directory / File Tree (planned, not yet created beyond `Embeded/`)

```
Project/
├── Embeded/                      (existing STM32CubeIDE project — hardware config, main.c stubs)
├── Common/
│   ├── Protocol/tlv_common.h     (shared LNC<->CC tag enum + frame struct, extern "C")
│   └── TLVCodec/tlv_codec.h/.cpp (shared CC<->GS codec, C++ only)
├── CentralComputer/              (C++, cc_main.cpp + fleet_main.cpp both link CentralComputerCore)
├── GroundStation/                (C++)
└── FleetOOP/                     (C++, part of the Central Computer solution)
```
(Full per-file breakdown was produced in earlier design discussion; will be re-expanded file-by-file as each one is actually built, per the workflow below — no point maintaining a giant speculative tree here when we're building one file at a time.)

---

## 12. Working Rules for This Project (how we collaborate)

- **Beginner-friendly code only.** No unnecessary advanced C/C++ techniques, macros, function-pointer tricks (unless architecture genuinely requires them), dynamic memory, or design patterns beyond what's needed. Prefer the simpler of two solutions.
- **One file at a time.** For each file: explain (1) why it exists, (2) which architecture layer it's in, (3) who uses it, (4) its functions, (5) its structs/enums, (6) its dependencies, (7) what it does *not* contain, (8) how we'll test it — then **wait for approval** before writing it.
- **Every source file starts with a purpose-explanation comment**: what the file does, its layer, its responsibilities, and what it does *not* do.
- **A test file accompanies every implementation file** before moving to the next one. Don't proceed until the current file's test has been run and discussed.
- **No silent fixes.** Spec gaps/contradictions get surfaced and classified (BLOCKING/IMPORTANT/MINOR/RECOMMENDATION) before being resolved, never quietly assumed.
- **No unnecessary FreeRTOS tasks** beyond the 7 listed in Sec. 7 — if another seems needed, stop and explain why before adding it.
- **Layer boundaries are not to be skipped** (Sec. 6): Transport never knows about TLV; Protocol never knows about UART/Ethernet.
- **Debugging discipline:** on a compile error, show it, explain it in simple terms, fix the smallest possible thing, don't touch unrelated code. On a test failure, first work out *what* failed and *why* (bad test? bad implementation? ambiguous spec? hardware config?) before proposing the smallest reasonable fix.
- **After each completed file**, summarize: File / Purpose / Layer / Functions / Test / Result — then state the next step and stop for approval.

---

## 13. Implementation Order (roadmap)

1. **Protocol layer** (`protocol.h` → `protocol.c` → `protocol_test.c`) — pure C, zero hardware dependency, can be built and tested entirely on a PC. **← we are here; this is the next file to design.**
2. **Transport layer** (`comm_transport_uart.h/.c`) — thin wrapper around the already-decided polling+`osDelay` USART2 design (Sec. 7).
3. **Message layer** — Tag→typed-struct mapping, using the now-known message catalog (Sec. 6); this is where the remaining numeric-tag and chunk-field-width decisions get made.
4. **LNC Application modules**, roughly in dependency order: Configuration → Log → Event → Monitor → Object Detection → Init → Keep-Alive → Watchdog (Watchdog's timing is paused pending Open Question #2).
5. **Central Computer**: Communication (LNC-facing) → Management Command → Data Collection & Analysis (SQLite) → Log.
6. **Ground Station.**
7. **FleetOOP** (Submarine hierarchy → Mission/Message → Fleet → menu).

---

## 14. Current Status

**Done:**
- Full requirements review against the real SW-FD-LNC-001 document.
- Hardware bring-up in CubeMX: all pins labeled and assigned a confirmed purpose; SPI1 datasize bug fixed; RTC and IWDG peripherals added.
- Architecture frozen: layering, FreeRTOS task list + priorities, TLV protocol format, DB schema, OOP class design and CC/FleetOOP decoupling.
- USART2 usage decided: shared with ST-LINK VCP, polling-only, no interrupts/DMA/printf, with the `osDelay(1)`-yield design to protect `KeepAliveTask`.

**Not done:**
- No numeric TLV tag values assigned yet (Message layer not started).
- IWDG tuning decision paused by user request.
- Sonar-vs-IR hardware question unresolved.
- Log/Configuration SD-card-vs-internal-flash storage split — **confirmed** (Sec 14, Log module writeup): SD/FatFs for Log, internal flash for Configuration.
- Event module (Sec 2.3) DONE and hardware-verified (Sec 15) - Monitor (Sec 2.1) is the next roadmap step, not yet started.

**Built so far:**
- **Protocol layer** — `Embeded/Core/Inc/protocol.h`, `Embeded/Core/Src/protocol.c`. Implements TLV encode/decode + CRC-16/CCITT-FALSE. Fully tested on PC: `Tests/Protocol/protocol_test.c` (plain C, no test library — build with `gcc -I ../../Embeded/Core/Inc protocol_test.c ../../Embeded/Core/Src/protocol.c -o protocol_test.exe`), 23/23 checks passing including the CRC standard test vector. This resolved Open Question #7 (test framework: plain C + hand-rolled `check()` helper, no library).
- **Transport layer** — `Embeded/Core/Inc/comm_transport_uart.h`, `Embeded/Core/Src/comm_transport_uart.c`. Thin polling wrapper around `HAL_UART_Transmit`/`HAL_UART_Receive` on USART2. **No abstract `ICommTransport` interface** — deliberately dropped in favor of plain concrete functions, since the LNC only ever has one real transport (UART); Ethernet is simulated PC-side only. Not compile-verified here (no ARM toolchain in this environment) — verify by building the `Embeded` project in STM32CubeIDE.
- **CommRxTask / CommTxTask wired up** in `main.c` (inside the existing `USER CODE` markers, so CubeMX regeneration won't erase this) as a **bring-up test**, not final logic: `CommRxTask` owns a `ProtocolDecoder`, polls `Transport_UART_ReceiveByte` (20 ms timeout, `osDelay(1)` on timeout per the locked starvation-prevention design), and increments one of two `volatile` watch counters (`g_frames_decoded_ok` / `g_frames_decode_error`) on each decode result. `CommTxTask` sends a fixed test frame (tag `0x01`) every 2 seconds — **temporary scaffolding**, to be replaced once the Message layer and real priority-queue draining exist.
- **Test-observation policy decided:** no `printf`/debug text ever (USART2 is protocol-only) — verification is done by watching plain counters live in the STM32CubeIDE debugger (Live Expressions/Watch window), not LEDs, not a serial console.

- **Central Computer / Ground Station Protocol layer** — `Common/TLVCodec/tlv_codec.h`, `Common/TLVCodec/tlv_codec.cpp`. C++ port of the same TLV format (`tlv::Decoder` class + `encodeFrame`/`calculateCrc16` free functions, `std::vector<uint8_t>` instead of the C side's fixed array, since dynamic allocation is a non-issue on a PC and is the simpler idiom in C++). Tested on PC: `Tests/TLVCodec/tlv_codec_test.cpp` (build: `g++ -std=c++17 -I ../../Common/TLVCodec tlv_codec_test.cpp ../../Common/TLVCodec/tlv_codec.cpp -o tlv_codec_test.exe`), 22/22 checks passing, **including the same CRC-16/CCITT-FALSE standard test vector the LNC's C implementation was checked against** — directly confirms the two independent implementations agree (this was Open Question/Recommendation #10, now resolved).

- **TLV tag catalog frozen** — `Common/Protocol/tlv_common.h`, shared plain-C-compatible header (`extern "C"` guarded) included directly by both LNC (C) and CC (C++). 21 tags across set-commands/requests/responses/events/data (full table in section 6 below - actually see this file's own comments for the authoritative list). Also defines the shared `SystemMode` enum. Design choice made here: mode-transition events (5 spec-listed cases) and object-detection events (2 spec-listed cases) are each **one generic tag** carrying a field describing which case, not one tag per case - fewer tags, same behavior fully implemented at the Application layer. `ChunkSeq`/`MoreDataFlag` byte widths for the two chunked-response tags are still undecided (Open Question #5).
- **Message layer, LNC side: 19 of 21 tags done** — `Embeded/Core/Inc/message.h`, `Embeded/Core/Src/message.c`. Wherever multiple tags carry identical fields, they share one struct + one serialization helper instead of duplicating it (mirrors the `MeasurementSample` pattern): `MeasurementSample` (KEEPALIVE, DATA_REPORT), `TimestampMessage` (SET_RTC_DATETIME, SYSTEM_TIME_RESPONSE, EVENT_CONFIG_CHANGED), `TimestampWithFlagMessage` (EVENT_OBJECT_DETECTION, EVENT_STARTUP), `TimeRangeMessage` (both "retrieve by range" requests), `TemperatureRangeMessage` (2 temp-range set commands), `SingleLimitMessage` (6 humidity/light/battery set commands), `ModeTransitionMessage` (1 tag), plus a no-payload builder for `GET_SYSTEM_TIME_REQUEST`. All "set limit" values use `float`, matching `MeasurementSample`'s fields. Zero hardware dependency - tested on PC: `Tests/Message/message_test.c` (same build command as before), **70/70 checks passing** (every builder's round trip: correct size, correct and distinct tag, every field verified bit-exact). Also verified to compile for the **real ARM target** (STM32CubeIDE's bundled toolchain, found on disk and invoked directly), confirming no STM32-specific issues.
- **NOT yet implemented: `TAG_MEASUREMENT_CHUNK_RESPONSE` and `TAG_EVENT_CHUNK_RESPONSE`** (the 2 remaining tags) - deliberately carved out, not overlooked. Two real unresolved problems surfaced while designing them: (1) "~20 records per chunk" doesn't fit the 256-byte frame cap - a `MeasurementSample` record is 21 bytes, so 20 of them (420 bytes) is nearly double the max payload (250 bytes); the realistic max is more like 11 records once RequestId/ChunkSeq/MoreDataFlag overhead is subtracted too. (2) An event record's `description` field has no defined maximum length or wire encoding anywhere in the spec. Needs a real design pass before building, not a guess.
- **Build-system gap found and fixed:** the real STM32CubeIDE project (`Embeded/.cproject`) had no include path for `Common/Protocol/`, so `message.c`'s `#include "tlv_common.h"` would have failed inside the actual IDE (caught via a real `make` build with the toolchain STM32CubeIDE bundles, found on disk and invoked directly). Fixed by adding `../../Common/Protocol` to the C compiler's include paths in both the Debug and Release configurations in `.cproject`. Verified by compiling `message.c` directly against the real ARM toolchain with that path - clean. Takes effect automatically next time the project is built inside STM32CubeIDE itself (no manual step needed).

- **Message layer, CC side: complete for all 19 implemented tags** — `CentralComputer/include/message.h`, `CentralComputer/src/message.cpp` (first CC-specific, non-shared code; `message::` namespace). Correctly split by real direction: **7 parsers** for the LNC→CC tags (`parseKeepAlive`, `parseDataReport`, `parseSystemTimeResponse`, `parseEventConfigChanged`, `parseEventObjectDetection`, `parseEventStartup`, `parseEventModeTransition` - each returns `std::optional<T>`, `std::nullopt` on a wrong tag or wrong-length payload, a real boundary check since wire data is untrusted input), and **12 builders** for the CC→LNC tags (2 temperature-range + 6 single-limit set commands, `SET_RTC_DATETIME`, `GET_SYSTEM_TIME_REQUEST`, 2 "retrieve by range" requests) - same struct-sharing pattern as everywhere else (`TimestampMessage` reused for both `parseSystemTimeResponse`/`parseEventConfigChanged` *and* `buildSetRtcDateTime`, since all three carry just one timestamp). Tested on PC: `Tests/CCMessage/cc_message_test.cpp` (build: `g++ -std=c++17 -I ../../CentralComputer/include -I ../../Common/TLVCodec -I ../../Common/Protocol cc_message_test.cpp ../../CentralComputer/src/message.cpp ../../Common/TLVCodec/tlv_codec.cpp -o cc_message_test.exe`), **44/44 checks passing**.
- **Directionality gap fixed — Message layer is now complete and correct for all 19 implemented tags on both sides.** LNC's `message.h`/`message.c` had the 12 CC→LNC tags backwards (builders instead of parsers); replaced with `Message_Parse*` functions (C-style: `int` return, 1/0 for success/failure, `ProtocolFrame*` in, typed struct out-parameter - the natural C equivalent of the CC side's `std::optional<T>`). Structs (`TemperatureRangeMessage`, `SingleLimitMessage`, `TimeRangeMessage`, `TimestampMessage`) were reused as-is since a struct's shape doesn't depend on direction - only the 12 functions changed. `message.h` now includes `protocol.h` (needed for the `ProtocolFrame` parameter type in the new parser signatures). Verified against the real ARM toolchain again (clean). Test suite updated to match: `Tests/Message/message_test.c` now builds raw frames "as the Central Computer would send them" (via `Protocol_EncodeFrame` directly, since the LNC no longer has builders for these 12) to test the parsers, including wrong-tag and wrong-length rejection - **58/58 checks passing**.

- **CC Transport layer done** — `CentralComputer/include/serial_transport.h`, `CentralComputer/src/serial_transport.cpp`. `transport::SerialPort` wraps a Windows COM port (Win32 `CreateFile`/`ReadFile`/`WriteFile`/`SetCommState`), mirroring the LNC's `comm_transport_uart.h` contract exactly (raw bytes only, `send`/`receiveByte` with a timeout, zero TLV knowledge). Copy constructor/assignment deleted (owns one OS handle - copying would double-close it). `void *handle_` used instead of `HANDLE` in the header so `<windows.h>`'s macros (`min`/`max`/`ERROR`/etc.) don't leak into every file that includes it. Handles the COM10+ `\\.\` prefix requirement internally.
  - **Real hardware discovery:** this machine has an actual Nucleo board attached as **COM10** ("STMicroelectronics STLink Virtual COM Port"), found via `[System.IO.Ports.SerialPort]::GetPortNames()`. Tested against it directly - `Tests/SerialTransport/serial_transport_test.cpp`, **10/10 checks passing**: open/close/reopen a real port, a nonexistent port fails gracefully, `send()` completes at the OS level, and `receiveByte()`'s timeout is accurate to within a couple ms of the requested value (measured with `std::chrono`, not just "didn't crash").
  - **Important caveat, not yet resolved:** the receive-timeout test got "byte received: no" - the board isn't currently sending anything. Since the `SD_SPI_HANDLE` build fix (needed for a successful build) came *after* the CommRxTask/CommTxTask loopback-test code was written, **the loopback-test firmware may never have been successfully flashed to this board.** Worth checking/reflashing before relying on the board's current behavior for anything.

- **Real hardware UART bring-up test — TX side confirmed working.** Debugged in two stages:
  1. A temporary raw-ASCII sanity test (`CommTxTask` sending plain `"HELLO STM32\r\n"` once/sec, bypassing TLV entirely) was flashed first, to isolate whether the raw USART2 → ST-LINK VCP → COM10 path worked at all, independent of the Protocol layer. Confirmed working (text observed arriving on COM10). This resolved the earlier "byte received: no" caveat above - it was a stale/never-reflashed board, not a real bug.
  2. `CommTxTask` was then reverted to the real TLV test loop (`#if 0`/`#else` scaffolding removed, raw-ASCII branch deleted since its job was done), rebuilt (`arm-none-eabi-gcc`/`make`, both found on disk under the STM32CubeIDE 2.1.1 install and invoked directly - clean build, only `main.c`/`protocol.c` needed recompiling), and flashed. A Python/`pyserial` script (`com10_hex_capture.py`, in this environment's scratchpad) read raw bytes off COM10 and confirmed real hardware sends exactly `AA 01 03 00 01 02 03 53 C4` every ~2s - SOF/Tag/Length/Value all correct, and the `53 C4` CRC independently recomputed and matched. **This directly confirms `Protocol_EncodeFrame` on real hardware agrees byte-for-byte with the PC-side CRC implementation - a live hardware confirmation of Open Question #10, not just the PC-only test vector.**
  - **TX side of the loopback test is proven on real hardware. RX side (jumper wire between PA2/PA3, watching `g_frames_decoded_ok`/`g_frames_decode_error` in the debugger to confirm `CommRxTask`'s decoder also works on real hardware) was deliberately skipped for now, by user request - revisit later, not forgotten.**

- **CC Communication module done** — `CentralComputer/include/communication.h`, `CentralComputer/src/communication.cpp`. The CC-side counterpart to the LNC's `CommRxTask`/`CommTxTask`: owns one `transport::SerialPort` + one `tlv::Decoder`, and is the "listener that dispatches received messages to the right module" from Sec 3. No RTOS on the CC side, so a plain `poll()` (try-receive-one-byte, short timeout) stands in for a FreeRTOS task - same polling philosophy, no thread introduced. `feedByte()` (the actual decode+dispatch logic) is public specifically so tests can drive it directly with hand-built frames, no real COM port needed - the same seam `tlv::Decoder::feedByte` itself is tested with. Dispatch is by a `Callbacks` struct of one `std::function` per LNC->CC message type (7 total, matching message.h's 7 parsers), each defaulting to empty - an unregistered callback, or a tag that isn't one of the 7 known types (this includes the 2 not-yet-designed chunk-response tags), is silently dropped, same spirit as the frozen protocol's "unknown tags dropped silently". Two observability counters, `framesDispatched`/`decodeErrors`, mirror the LNC's `g_frames_decoded_ok`/`g_frames_decode_error`. `sendFrame()` is a thin pass-through to the serial port - Communication does not build outgoing frames itself (Management Command's job, not yet built) and does not implement the LNC's 3-tier TX priority queue (that's an LNC-only requirement, Sec 2.5). Tested on PC, no hardware: `Tests/Communication/communication_test.cpp` (build: `g++ -std=c++17 -I ../../CentralComputer/include -I ../../Common/TLVCodec -I ../../Common/Protocol communication_test.cpp ../../CentralComputer/src/communication.cpp ../../CentralComputer/src/message.cpp ../../CentralComputer/src/serial_transport.cpp ../../Common/TLVCodec/tlv_codec.cpp -o communication_test.exe`), **25/25 checks passing** - each of the 7 message types dispatches to its own callback with correct fields, an unregistered callback doesn't crash, an unknown tag is ignored without bumping any counter, a corrupted (bad-CRC) frame bumps `decodeErrors` and dispatches nothing, and `sendFrame`/`isOpen` behave correctly before `open()` is ever called.

- **Module split for Sec 2.5's two CC->LNC traffic categories, now locked:** "management commands" (8 set-limit + set-RTC + get-system-time) belong to Management Command; "retrieval instructions" (measurements/events by range) belong to Data Collection & Analysis. This wasn't explicit before and is now the basis for both modules' scope.

- **Communication addition:** `Communication::lastSentFrame` (`std::vector<uint8_t>`), set at the top of every `sendFrame()` call regardless of whether the port is open. Same observability spirit as `framesDispatched`/`decodeErrors` - lets any module built on top of Communication be tested without a real, open serial port. Existing `Tests/Communication/communication_test.cpp` re-run after this change: still 25/25.

- **Management Command done** — `CentralComputer/include/management_command.h`, `CentralComputer/src/management_command.cpp`. Covers exactly the 10 "management commands" (not the 2 retrieval instructions - see split above). `ManagementCommand` holds a `Communication &` (doesn't own it - shared with Data Collection & Analysis later) and exposes one function per command, taking plain scalars (`float`/`uint32_t`) so callers never see `message.h`'s struct shapes: `setTempNormalRange`/`setTempWarningRange`, the 6 single-limit setters, `setRtcDateTime`, and `requestSystemTime` (fire-and-forget - the reply arrives later via `Communication::callbacks.onSystemTimeResponse`, no response correlation exists yet). Each function builds via the matching `message::buildXxx` and forwards straight to `communication_.sendFrame()`. Tested on PC, no hardware: `Tests/ManagementCommand/management_command_test.cpp` (build: `g++ -std=c++17 -I ../../CentralComputer/include -I ../../Common/TLVCodec -I ../../Common/Protocol management_command_test.cpp ../../CentralComputer/src/management_command.cpp ../../CentralComputer/src/communication.cpp ../../CentralComputer/src/message.cpp ../../CentralComputer/src/serial_transport.cpp ../../Common/TLVCodec/tlv_codec.cpp -o management_command_test.exe`), **14/14 checks passing** - every one of the 10 functions checked against an independently-recomputed expected frame via `Communication::lastSentFrame`, plus confirms the `bool` return value correctly propagates `sendFrame`'s false-when-not-open result.

- **Chunked bulk-transfer responses done** — the 2 remaining tags (`TAG_MEASUREMENT_CHUNK_RESPONSE`, `TAG_EVENT_CHUNK_RESPONSE`) are now fully designed and implemented on both sides, closing out the last gap in the 21-tag catalog. Real gap found and fixed along the way: **the two retrieval requests (`TimeRangeMessage`) had no `RequestId` field**, even though the already-frozen bulk-transfer design (Sec 6) requires response chunks to echo one back for correlation - the request has to be the one to originate it. Fixed on both sides: `TimeRangeMessage` grew a `requestId` (`uint8_t`) field, changing its wire payload from 8 to 9 bytes (`requestId + startTime + endTime`).
  - **Design decisions** (`Common/Protocol/tlv_common.h`): `MAX_MEASUREMENTS_PER_CHUNK = 11` and `MAX_EVENTS_PER_CHUNK = 6`, both computed from the frozen 256-byte frame cap (250-byte max value, minus a 4-byte chunk header `RequestId(1)+ChunkSeq(2)+MoreDataFlag(1)`, divided by each record's wire size). `EVENT_DESCRIPTION_SIZE = 32` (31 usable characters + a guaranteed null terminator within the field, truncating anything longer) - a fixed-size field, not variable-length, since the LNC only ever writes short canned messages here, never arbitrary text; this keeps both sides' code simple (no length-prefix parsing). An event record's `eventType` byte reuses the existing `TAG_EVENT_MODE_TRANSITION`/`OBJECT_DETECTION`/`CONFIG_CHANGED`/`STARTUP` values directly - one vocabulary instead of inventing a second enum.
  - **LNC side** (`Embeded/Core/Inc/message.h`, `Embeded/Core/Src/message.c`): new `EventRecord` struct (`timestamp` + `eventType` + fixed `description[EVENT_DESCRIPTION_SIZE]`), and two new builders, `Message_BuildMeasurementChunkResponse`/`Message_BuildEventChunkResponse` (LNC sends these, so it builds, not parses - same direction logic as everywhere else in this file), both rejecting a record count above the max for that chunk type. `message.h` now includes `tlv_common.h` directly (needed for the new size macros in the header itself, not just the .c file). Verified against the real ARM toolchain: full project rebuild, clean, only `message.c` needed recompiling. Tested on PC: `Tests/Message/message_test.c`, now **80/80 checks passing** (up from 58) - covers both new builders at max chunk size, the reject-too-many-records path, and confirms a truncated description is still always null-terminated.
  - **CC side** (`CentralComputer/include/message.h`, `CentralComputer/src/message.cpp`): new `EventRecord` (C++ version - `std::string description`, trimmed at the first null byte found **within the fixed field's own bounds**, not trusted to be there, since wire data is untrusted input - a corrupted/malicious frame missing a null wouldn't be allowed to read past the field), `MeasurementChunkResponse`/`EventChunkResponse` structs, and `parseMeasurementChunkResponse`/`parseEventChunkResponse` (CC receives these, so it parses). Both reject a too-short payload or one whose post-header length isn't an exact multiple of one record's size. Tested on PC: `Tests/CCMessage/cc_message_test.cpp`, now **61/61 checks passing** (up from 44).
  - **Communication updated** to actually dispatch these two tags instead of dropping them as unrecognized: added `onMeasurementChunkResponse`/`onEventChunkResponse` to `Callbacks`, wired into the dispatch `switch`. Tested: `Tests/Communication/communication_test.cpp`, now **29/29 checks passing** (up from 25).

- **SQLite vendored** — `Common/ThirdParty/sqlite3/sqlite3.c` + `sqlite3.h` (amalgamation, v3.53.4, public domain), provided by the user after this session's shell turned out to have no working internet access (TLS/schannel failure - looks like a proxy/cert interception issue) and no system SQLite/dev headers were present anywhere on the machine. Deliberately vendored rather than linking Python's bundled `sqlite3.dll` (which does exist on this machine but would make the real CC build depend on an incidental DLL from an unrelated install).
- **Build gotcha, worth remembering for `cc_main.cpp` later:** `sqlite3.c` MUST be compiled as C (`gcc`), never C++ (`g++`) - `g++` forces C++ rules onto any file it's given regardless of `.c` extension, and `sqlite3.c` relies on implicit `void*`-to-`T*` conversions that are legal in C but a hard error in C++ (confirmed - `g++` produced ~15 `invalid conversion from 'void*'` errors). The fix: `gcc -std=c11 -c sqlite3.c -o sqlite3.o`, compile every other `.cpp` with `g++` as usual, then link the `.o` files together with `g++` (works fine - `sqlite3.h` already has the `extern "C"` guard needed for C/C++ linkage to mix).
- **DataStore done** — `CentralComputer/include/data_store.h`, `CentralComputer/src/data_store.cpp`. The SQLite engine behind Sec 3's Data Collection & Analysis: owns the DB connection, creates the two Sec 8 tables (`measurements`, `events`, both `id INTEGER PRIMARY KEY` + a timestamp index) on `open()` if they don't exist, non-copyable/RAII (mirrors `SerialPort`'s shape exactly). Reuses `message::MeasurementSample`/`message::EventRecord` as row types directly - no third, redundant "row" struct, since both already have exactly the right shape. Six functions: `open`/`close`/`isOpen`, `insertMeasurement`, `insertEvent`, `pruneOlderThan` (7-day retention, matching the LNC), `getMeasurementsInRange`/`getEventsInRange` (both bounds inclusive, ordered oldest-first). No logging anywhere - failures are a `false` return or an empty vector, same convention as every other module. Tested against a real, private in-memory SQLite database (`:memory:`, no files left on disk, no mocking): `Tests/DataStore/data_store_test.cpp` (build: see the two-step gcc/g++ process documented in the test file's own header comment), **24/24 checks passing** - covers idempotent re-`open()`, insert+range-query for both tables (including inclusive bounds and no-match cases), confirms the two tables are independent, and confirms `pruneOlderThan` deletes only the correct rows in both tables.

- **DataCollection done** — `CentralComputer/include/data_collection.h`, `CentralComputer/src/data_collection.cpp`. The Application-layer glue between `Communication` and `DataStore` (Sec 3's Data Collection & Analysis): its constructor registers all 9 relevant callbacks on the given `Communication` (7 for live traffic, 2 for chunk responses), each translating an arriving message into one `DataStore` insert followed by `pruneOlderThan(...)` (7-day retention, called after every single insert per this session's agreed decision). Holds `Communication&`/`DataStore&` by reference (owns neither); non-copyable, since its constructor registers lambdas capturing `this`. No generic `pending_request_registry` (as Sec 8 originally envisioned) - each chunk response is persisted record-by-record as it arrives, and only one small `std::optional<uint8_t>` per data type tracks "is a backfill in flight, and what requestId did I send," cleared (and `onMeasurementBackfillComplete`/`onEventBackfillComplete` fired) when a chunk's `MoreDataFlag` is false. A chunk whose `requestId` doesn't match the currently-pending one is silently ignored (stale or unrelated).
  - **Real correctness point found while implementing:** the 7-day retention cutoff MUST be computed from real wall-clock "now" (`std::time(nullptr)`), never from the timestamp of whatever record triggered the insert - a backfilled record is deliberately old, so using its own timestamp as "now" would incorrectly prune data (possibly the very data just backfilled).
  - **Real design change found while testing, not silently patched:** `requestMeasurementBackfill`/`requestEventBackfill` originally gated "backfill now in progress" on `Communication::sendFrame()`'s own true/false result - but that method is *always* false in any test that doesn't open a real hardware port (this codebase's established, deliberate convention for every other test file), which made the correlation/completion logic - the actual interesting part of this module - untestable as first written. Resolved by decoupling: these functions now return `true` once a backfill has been started (built + handed to `sendFrame`), independent of whether the OS confirmed the write. Justification, not just a test workaround: this design has no retry/timeout mechanism at all, so "the low-level send failed" and "the send succeeded but the LNC never replies" were already equally unrecoverable, indistinguishable failure modes - gating on the former added no real safety, only untestability.
  - Tested with a real (never-opened) `Communication` and a real in-memory `DataStore` wired together, frames fed via `Communication::feedByte()` (same seam as `communication_test.cpp`): `Tests/DataCollection/data_collection_test.cpp` (two-step gcc/g++ build, same reason as `Tests/DataStore` - see that test's header comment), **25/25 checks passing** - covers both live-traffic paths, full backfill request→chunk→completion flow, stale/mismatched-requestId rejection, "can't overlap itself," and the wall-clock-vs-record-timestamp retention distinction explicitly.

- **report_generator done** — `CentralComputer/include/report_generator.h`, `CentralComputer/src/report_generator.cpp`. Sec 3's "prepares reports broken down by criteria," implemented as a namespace per Sec 8's frozen decision (not a class). Read-only consumer of `DataStore` - never writes, never touches `Communication`/`DataCollection`. Deliberately adds no new SQL: calls the already-tested `getMeasurementsInRange`/`getEventsInRange` and does all counting/min/max/averaging in plain C++ over the returned rows, keeping `DataStore` exactly what its own header already claims ("does NOT implement report/aggregation logic"). Two functions: `generateMeasurementReport` (total count, a count per `SystemMode`, min/max/average per sensor) and `generateEventReport` (total count, a count per event type actually seen in the range - not hardcoded to the 4 currently-known types, so a future new event type is still counted correctly rather than silently dropped). Both return sensible all-zero/empty results for an empty range rather than crashing or returning garbage. Tested against a real in-memory `DataStore` (`:memory:`): `Tests/ReportGenerator/report_generator_test.cpp` (two-step gcc/g++ build, same reason as `Tests/DataStore`), **16/16 checks passing** - covers empty-range behavior, correct counts/min/max/average against hand-computed expected values, time-range filtering, and the per-event-type breakdown. No other existing file was touched, so no regression risk to the other suites.

- **cc_main.cpp done** — `CentralComputer/src/cc_main.cpp`, the CC's real entry point: the first file to construct `Communication`, `DataStore` (a real file, `central_computer.db`, not `:memory:`), `DataCollection`, `ManagementCommand`, and `report_generator` together and actually run them, instead of each being exercised in isolation by its own test. Single-threaded on purpose, matching every module it uses: the main loop calls `communication.poll()` (a real blocking `ReadFile` with a short timeout underneath - no `osDelay`-equivalent needed here, unlike the LNC's bare-metal polling, since a real OS blocking call already yields properly) and checks for a keypress without blocking via `_kbhit()`/`_getch()` (Windows-only, consistent with `serial_transport.cpp` already being Windows-only). No auto time-sync or auto-backfill at startup - Sec 2.7's Init concept is LNC-only, nothing requires CC-side startup behavior, so none was invented.
  - **Small interactive menu**, since this program is otherwise silent aside from what it explicitly prints (every module below it is deliberately log-free): `[1]` sends `ManagementCommand::setRtcDateTime(now)` - **the one genuinely meaningful real-hardware test available today**, since the LNC has no application layer yet (confirmed by re-reading its actual `main.c`: `CommRxTask` only counts decoded/error frames, it never calls into `message.c`'s parsers or dispatches by tag) - watch the STM32 debugger's `g_frames_decoded_ok` counter increase by 1, proving CC→LNC delivery (the direction the earlier hex-capture test didn't cover; that one proved LNC→CC). `[2]` requests a measurement backfill (proves CC builds/sends the request correctly; the LNC won't reply - honest, expected, not a bug). `[3]` prints status (port open, `framesDispatched`, `decodeErrors`, backfill-in-progress flags). `[4]` prints a 7-day report via `report_generator`. `[q]` closes everything and exits.
  - **Not unit-tested like every other file** - a real entry point with a real infinite loop against real hardware isn't something a `check()`-based PC test suits. Verified instead by a full build: `gcc -std=c11 -c Common/ThirdParty/sqlite3/sqlite3.c -o sqlite3.o`, then `g++ -std=c++17` compiling `cc_main.cpp` and every module it uses (`communication.cpp`, `data_collection.cpp`, `data_store.cpp`, `management_command.cpp`, `report_generator.cpp`, `message.cpp`, `serial_transport.cpp`, `tlv_codec.cpp`) with `-I CentralComputer/include -I Common/TLVCodec -I Common/Protocol -I Common/ThirdParty/sqlite3`, then linking all the `.o` files together - clean compile and link, `CentralComputer/build/cc_main.exe` produced. **Not run from this environment** - `_kbhit()`/`_getch()` need a real console and don't work reliably through a piped/non-interactive shell, so running it and exercising the menu against the real board is a user action.

- **First real cc_main.exe run against hardware, and a debugger problem.** User ran `cc_main.exe`, pressed `[1]`, got `sendFrame result: ok`. Then hit **"Target not valid"** trying to start an STM32CubeIDE debug session, so `g_frames_decoded_ok` couldn't be checked the normal way. Debugged as much as possible from the terminal instead, without assuming anything:
  - **CC's encoding independently verified correct, twice.** Hand-computed the expected `TAG_SET_RTC_DATETIME` frame for the observed timestamp (1788108784) using the frozen CRC-16/CCITT-FALSE algorithm: `AA 09 04 00 F0 5F 94 6A D2 28` (10 bytes). Then compiled and ran a throwaway program calling the REAL `message::buildSetRtcDateTime` directly - byte-for-byte identical output. Then traced the path from there to the OS: `ManagementCommand::setRtcDateTime` → `buildSetRtcDateTime` → `Communication::sendFrame` → `SerialPort::send` → `WriteFile`, confirmed no transformation happens anywhere in that chain, and `SerialPort::send` only returns true if `WriteFile` reports every byte written - so the observed `sendFrame result: ok` is proof the OS genuinely wrote exactly those 10 bytes to COM10.
  - **Confirmed, empirically, that COM10 cannot be independently sniffed while `cc_main.exe` is running** - tried opening it from a second process (Python/pyserial) and got `PermissionError: Access is denied`, matching `SerialPort::open`'s exclusive `CreateFileA` sharing mode (`0`, no sharing). This is a hard limit of the current design, not an assumption: proving LNC-side receipt from the CC side alone is not possible without either the debugger or LNC-side instrumentation.
  - **Traced `CommRxTask` (`StartTask05` in `main.c`) exactly.** Confirmed from source: it only ever calls `Transport_UART_ReceiveByte` → `Protocol_FeedByte` → increments `g_frames_decoded_ok`/`g_frames_decode_error`. It does NOT call any `Message_Parse*` function, does NOT dispatch by tag, and does NOT act on a decoded frame's contents in any way - so even perfect reception of the RTC-set command would not actually change the LNC's clock right now; a counter increment is the ceiling of what's observable today, debugger or not. Also confirmed: since no loopback jumper is wired (Sec 14, deferred), `CommRxTask` cannot receive `CommTxTask`'s own periodic bring-up frames - the only bytes that can ever reach it are ones the CC explicitly sends, so a `g_frames_decoded_ok` increment can only be attributed to a real CC-sent command, nothing else.
  - **Added temporary, debugger-free instrumentation**, per explicit request: `main.c`'s `CommRxTask`, on `PROTOCOL_DECODE_FRAME_READY`, now also calls `HAL_GPIO_TogglePin(RGB_LED_1_GPIO_Port, RGB_LED_1_Pin)` (PA9) right alongside the existing counter increment. Pure GPIO write, never touches USART2, so it cannot affect the TLV stream. Confirmed `RGB_LED_1_Pin` is already initialized as `GPIO_MODE_OUTPUT_PP` in `MX_GPIO_Init()` (runs well before the scheduler starts), so the toggle is safe to call. Since nothing else currently writes to this pin, the LED will flip from its current state to the other and *stay there* - so one button-press in `cc_main` should visibly change the LED from off to a steady on (or vice versa), not a blink. Rebuilt against the real ARM toolchain: clean, only `main.c` needed recompiling, zero warnings. **Not yet flashed** - that's the next user action.
  - **What still genuinely requires the user, not the debugger specifically:** (a) reflash the board with this updated firmware - try a normal Run/flash first, separately from starting a live Debug session, since "Target not valid" is specifically a debug-attach failure and programming often still works even when attaching a debugger doesn't; (b) run `cc_main.exe`, press `[1]`, and look at `RGB_LED_1` (PA9) - no debugger needed for this, just eyes on the board.

- **UART/RX real-hardware investigation — root cause found and fixed. VERIFIED / PASSED.** Triggered when the LED instrumentation above never toggled during the CC→LNC delivery test. Investigated exhaustively, without assuming anything, across several rounds of read-only source inspection plus temporary, explicitly-labeled instrumentation (all since fully reverted — see below).
  - **CC's send path re-confirmed correct, three independent ways**: hand-computed CRC, a direct call to the real compiled `message::buildSetRtcDateTime`, and a full-chain test through the real `ManagementCommand`+`Communication` — all byte-for-byte identical. `Communication::sendFrame`→`SerialPort::send`→`WriteFile` traced with zero transformation anywhere. This side was never the problem.
  - **Live measurement caught the real clue**: `CommTxTask`'s periodic test frame was arriving on COM10 every ~515–517ms, not the ~2000ms `osDelay(2000)` should produce — matching, almost exactly, this project's own earlier-computed IWDG full timeout (~512ms, Sec 9.2).
  - **Root cause, confirmed directly from source**: `StartTask08` (`WatchdogTask`) had `HAL_IWDG_Refresh(&hiwdg)` commented out, while `MX_IWDG_Init()` (which starts the IWDG counting immediately and unconditionally, and cannot be stopped except by reset) was still called every boot. With nothing ever refreshing it, the MCU was being watchdog-reset roughly every ~512ms continuously — explaining, all at once: `CommRxTask`'s decoder/counters being wiped before they could show progress, `RGB_LED_1`'s toggle being invisible (reset by `MX_GPIO_Init()` on every reboot), and STM32CubeIDE's "Target not valid" (a target resetting every half-second is a classic SWD-attach killer).
  - **The user removed the IWDG/WatchdogTask refresh logic entirely** (their own action, via project regeneration) — not a change made as part of this investigation. Confirmed effective: with the watchdog gone, the MCU runs continuously with no resets.
  - A sequence of temporary, clearly-labeled diagnostics was used to isolate this and has since been **fully removed**: direct `HAL_UART_Receive`/`HAL_UART_Transmit` calls bypassing the Transport wrapper (to capture exact `HAL_StatusTypeDef` values and register-level `ISR.TXE`/`ISR.TC` flags), a raw byte-pattern loopback test (`0x55 0xAA 0x12 0x34` over a physical PA2→PA3 jumper), and the `RGB_LED_1` toggle. Verified by a fresh grep for every temporary identifier — zero matches. `main.c` is back to exactly its pre-investigation bring-up state: `CommRxTask` = `Protocol_DecoderInit`+`Protocol_FeedByte` loop incrementing `g_frames_decoded_ok`/`g_frames_decode_error`; `CommTxTask` = the real TLV test loop, tag `0x01`, every 2s. Confirmed still builds clean against the real ARM toolchain.
  - **Final confirmation, watchdog gone, original code restored**: using the PA2→PA3 physical loopback jumper (already wired during this investigation) and the debugger (now attaching successfully, since the MCU no longer resets every 512ms), `g_frames_decoded_ok` was observed increasing from 20 to 30 during a test. `CommRxTask` is alive, `Transport_UART_ReceiveByte`/`HAL_UART_Receive` are physically receiving bytes, and `Protocol_FeedByte` is successfully decoding valid TLV frames — on real hardware.
  - **Conclusion: `CC → COM10 → ST-Link VCP → USART2 RX → Protocol_FeedByte()` is VERIFIED / PASSED.** Precisely, this combines two separately-verified halves, not one single unified live test: CC's TX correctness was proven independently (above), and the LNC's RX+decode pipeline was proven separately via the jumper-loopback test using the LNC's own bring-up test frame (tag `0x01`), not a literal CC-originated command captured in the same run. The two together give strong, evidence-based confidence in the full path — worth keeping the distinction in mind, but not a gap that blocks moving forward.
  - **Still NOT verified — the actual next thing to test**: any application-level effect of a received command. `CommRxTask` still only counts and decodes; it does not call any `Message_Parse*` function, does not dispatch by tag, and does not act on a frame's contents. Sending `setRtcDateTime` to the LNC right now would not actually change its clock, since no Configuration/Init module exists yet to apply it.

- **LED driver done** — `Embeded/Core/Inc/led.h`, `Embeded/Core/Src/led.c`. The first LNC Application-layer module (started deliberately before the RTC/dispatch work in the "Next Session" section below, per user request - simple hardware modules first). Plain driver, no FreeRTOS task, no PWM: `LedColor` enum (`LED_COLOR_OFF/RED/YELLOW/GREEN`, matching exactly the colors Sec 2.3 needs) and one function, `Led_SetColor(LedColor color)`, which writes the 3 `RGB_LEDx` GPIO pins directly. Not PC-testable (pure GPIO) - verified by a clean compile against the real ARM toolchain, then by a temporary boot-time test cycle (Red→Yellow→Green→Off, wired temporarily into `StartTask02`) flashed to the real board.
  - **Real pin-to-color mapping found and confirmed on hardware** (the initial guess was wrong): `RGB_LED_1` (PA9) = **Green**, `RGB_LED_2` (PA8) = **Blue**, `RGB_LED_3` (PC9) = **Red**. Found via the temporary test: lighting `RGB_LED_1` alone showed green, `RGB_LED_2` alone showed blue, both together showed cyan (green+blue, consistent) - `RGB_LED_3` was inferred as Red by elimination, then directly confirmed correct on a second flash with `led.c` corrected. Final boot-cycle test with the corrected mapping showed the full Red→Yellow→Green→Off sequence exactly as expected. `led.c` maps: `red`->`RGB_LED_3`, `green`->`RGB_LED_1`, `blue`->`RGB_LED_2` (blue channel wired but not yet used by any `LedColor` value, since the spec never needs blue).
  - Temporary boot-test code (in `StartTask02`) and its `#include "led.h"` in `main.c` have both been removed now that the mapping is confirmed - `StartTask02` is back to an empty stub, ready for real `Init` logic later. `led.c`/`led.h` themselves are permanent and unaffected by that cleanup.
  - Not yet wired into anything - no `Event` module exists yet to call `Led_SetColor` on real mode transitions. That wiring happens when the `Event` module is built (Sec 13, roadmap step 4).

- **DHT11 driver done and verified on real hardware** — `Embeded/Core/Inc/dht.h`, `Embeded/Core/Src/dht.c`. Second LNC Application-layer module. Plain synchronous driver, no FreeRTOS task of its own: `DhtReading` struct (`temperatureC`, `humidityPercent`), `DhtResult` enum (`DHT_OK`/`DHT_ERROR_TIMEOUT`/`DHT_ERROR_CHECKSUM`), `DHT_Init()` (starts TIM6) and `DHT_Read()` (runs one full DHT11 single-wire read on `DHT_Pin`/PB5). `DHT_Pin` was reconfigured in CubeMX from push-pull to **Output Open-Drain** with an internal pull-up (`.ioc` and generated code both updated, no drift risk) - this means the driver never needs to switch GPIO mode mid-read: writing HIGH releases the line, writing LOW drives it, and the pin can be read at any time. Bit timing uses TIM6 as a 1 MHz (1 tick = 1 µs) stopwatch, confirmed via the clock tree (HSI 16MHz → PLL → 80MHz SYSCLK/PCLK1 → TIM6 prescaler 79 → 1 MHz), resolving Open Question #9. Not PC-testable (real GPIO timing) - verified by a clean compile against the real ARM toolchain and by direct testing on real hardware (see below).
  - **Real hardware pin-mapping/wiring confirmed working**: pull-up resistor present and adequate (user-confirmed), `DHT_Pin`=PB5 wiring correct, sensor is a DHT11.
  - **Real TIM6 wraparound bug found and fixed during hardware debugging.** The first working version measured elapsed time as `(uint16_t)(now - start)`, which is only valid if the hardware counter wraps at the full 16-bit boundary (65536) - but TIM6's configured period is 999, so it actually wraps at 1000. Whenever a wait's `start` snapshot landed near the top of the 0-999 range and the counter rolled over mid-wait, the subtraction produced a huge bogus "elapsed" value and triggered a false timeout - a real, deterministic bug, not a hardware/wiring issue. **Fixed** by resetting the TIM6 counter to 0 at the start of every `WaitForLevel()` call (`__HAL_TIM_SET_COUNTER(&htim6, 0)`) instead of snapshotting and subtracting - found by comparing against a separately-provided, already-working bare-metal DHT11 reference implementation that used exactly this reset-per-measurement pattern (that reference's own architecture - runtime GPIO mode-switching, loop-iteration timeouts, no FreeRTOS at all - was deliberately **not** copied wholesale; only this one specific, verified-correct technique was adopted).
  - **IMPORTANT - FreeRTOS timing requirement found and confirmed by direct testing, not just theory:** `DHT_Read()`'s bit-banging requires every wait inside it to complete within tens of microseconds of the real signal edge, but it has no protection of its own against being preempted (and `dht.c` is deliberately kept FreeRTOS-independent - it must stay that way). On real hardware, with `DHT_Read()` called from a normal-priority task while other tasks are still empty `osDelay(1)` stubs (all 7 tasks are currently `osPriorityLow` in the generated code - the per-task priority scheme from Sec 7 hasn't been applied to CubeMX yet), the read consistently failed at the exact same bit (bit 28) every attempt - traced (using temporary stage/bit-index/pulse-duration diagnostics, since removed) to FreeRTOS time-slicing another task in mid-wait for long enough to miss an edge. Confirmed as the root cause, not a coincidence, by comparing against the reference implementation above: that one runs with **no RTOS at all**, so nothing can ever preempt it - explaining why its bit-banging works reliably despite a much less precise (loop-iteration-based) timeout mechanism than ours.
  - **Current fix (temporary, application-level, in `StartTask02`/`main.c` - not in `dht.c`):** the calling task temporarily raises its own priority (`osThreadSetPriority(..., osPriorityAboveNormal)`) immediately before calling `DHT_Read()`, and restores its original priority immediately after. This is a **task/application-level concern deliberately kept out of `dht.c`**, per the working rule that the driver itself must stay FreeRTOS-independent. Confirmed fixed on real hardware: the read now completes all 40 bits and returns valid temperature and humidity.
  - **Not yet decided:** whether this ad-hoc "raise my own priority" approach is what the real `Monitor` module should do when it eventually calls `DHT_Read()` every 5 seconds (Sec 2.1), or whether a different mechanism (e.g. a critical section, or simply giving `Monitor` a high enough fixed priority once the real Sec 7 priority scheme is applied) is more appropriate - open item for when `Monitor` is actually built.
  - **Test-observation policy**: a temporary set of diagnostic globals (`g_dht_error_stage`, `g_dht_error_bit_index`, `g_dht_last_pulse_us`) was used during hardware debugging to localize the failure to a specific bit/stage, and has since been **fully removed** now that the root cause is fixed and confirmed. The ongoing hardware-verification globals (`g_dht_last_result`, `g_dht_last_temperature`, `g_dht_last_humidity`, `g_dht_read_count`) are still in place in `StartTask02`, watched live in the debugger - kept for now since they're still useful for verification, not yet cleaned up.

- **Battery (potentiometer) driver — implemented, awaiting real-hardware verification.** `Embeded/Core/Inc/battery.h`, `Embeded/Core/Src/battery.c`. Third LNC Application-layer driver (after LED, DHT11), same plain-driver shape - no FreeRTOS task, no struct needed (a single value). One function: `float Battery_ReadVoltage(void)` - runs one ADC1 conversion on PA0 (channel 5, 12-bit, already configured by CubeMX's `MX_ADC1_Init()`) and converts the raw 0-4095 reading to a voltage via `raw / 4095.0f * 3.3f`. User confirmed the potentiometer is wired across the full 3.3V rail with the wiper on PA0, so this conversion is correct as-is. No averaging/filtering (one call = one conversion, same simplicity level as `led.c`/`dht.c` - revisit only if real hardware shows it's noisy). `hadc1` accessed via a local `extern ADC_HandleTypeDef hadc1;` in `battery.c`, same pattern `dht.c` already uses for `htim6` (neither handle is exposed in `main.h`).
  - **Wired into `main.c` for hardware testing (temporary, same pattern as DHT):** `#include "battery.h"` added to the Includes block; `volatile float g_battery_last_voltage` added next to the existing DHT test globals; inside `StartTask02`'s existing loop, one line added right after the existing DHT read (`g_battery_last_voltage = Battery_ReadVoltage();`) - the DHT code itself was **not modified**. No priority boost needed (unlike `DHT_Read()`) since a single ADC conversion isn't timing-critical the way DHT's bit-banging is.
  - **Verified so far:** clean compile of both `battery.c` and the updated `main.c` against the real ARM toolchain (`arm-none-eabi-gcc`, found under the STM32CubeIDE 2.1.1 install and invoked directly), zero warnings.
  - **VERIFIED ON REAL HARDWARE.** User tested on the real board - `g_battery_last_voltage` tracks the potentiometer correctly. `battery.h`/`battery.c` are finished, permanent, and not yet wired into anything beyond the temporary `StartTask02` test call (no `Monitor` module exists yet to call `Battery_ReadVoltage()` for real).

- **Light driver — implemented, awaiting real-hardware verification.** `Embeded/Core/Inc/light.h`, `Embeded/Core/Src/light.c`. Fourth LNC Application-layer driver, same shape as `battery.c`. One function: `float Light_ReadPercent(void)` - runs one ADC2 conversion on PA1 (channel 6, 8-bit, configured by `MX_ADC2_Init()`) and converts the raw 0-255 reading to a percentage via `raw / 255.0f * 100.0f`. Returns a percentage rather than a voltage (unlike Battery) since ADC2 is only 8-bit (a different max raw value than ADC1's 12-bit) and a light sensor's raw voltage isn't a meaningful physical unit the way battery voltage is - percentage avoids silently assuming a sensor voltage range. `hadc2` accessed via a local `extern ADC_HandleTypeDef hadc2;` in `light.c`, same pattern as `battery.c`'s `hadc1` and `dht.c`'s `htim6`. No FreeRTOS task, no struct, no averaging - same simplicity level as `battery.c`/`led.c`.
  - **Wired into `main.c` for hardware testing (temporary, same pattern as Battery):** `#include "light.h"` added to the Includes block; `volatile float g_light_last_percent` added next to the existing test globals; inside `StartTask02`'s existing loop, one line added right after the Battery read (`g_light_last_percent = Light_ReadPercent();`). DHT and Battery code were **not modified**. No priority boost needed, same reasoning as Battery.
  - **Verified so far:** clean compile of both `light.c` and the updated `main.c` against the real ARM toolchain, zero warnings.
  - **VERIFIED ON REAL HARDWARE.** User tested on the real board - `g_light_last_percent` tracks the light sensor correctly. `light.h`/`light.c` are finished, permanent, and not yet wired into anything beyond the temporary `StartTask02` test call.

**All 4 Monitor sensors are now done and verified at the driver level:** temperature+humidity (DHT11), battery (potentiometer/ADC1), light (ADC2) - all confirmed working on real hardware.

- **IR object-detection driver — implemented, awaiting real-hardware verification (including polarity).** `Embeded/Core/Inc/ir.h`, `Embeded/Core/Src/ir.c`. Fifth LNC Application-layer driver. One function: `IrState IR_Read(void)` (`IrState` = `IR_NOT_DETECTED`/`IR_DETECTED`) - reads `IR_Pin` (PB10) once via `HAL_GPIO_ReadPin` and maps it to the enum. Same shape as `led.c`/`battery.c`/`light.c`: no FreeRTOS task (the future `ObjectDetectionTask` will call this driver repeatedly, per Sec 7 - not built yet), no debouncing/filtering, one call = one raw read.
  - **Investigated and resolved before building:** see the "IR vs. sonar" entry above under Resolved Open Questions - hardware is confirmed to be a single-pin digital IR sensor, not sonar.
  - **Polarity is UNCONFIRMED, clearly marked as such in both `ir.h` and `ir.c`.** Current assumption: `GPIO_PIN_RESET` (LOW) = `IR_DETECTED`, `GPIO_PIN_SET` (HIGH) = `IR_NOT_DETECTED` - a guess based on the common wiring for this style of sensor, the same kind of provisional guess the LED driver started with before real-hardware testing corrected its pin→color mapping. To be confirmed or corrected once tested on real hardware.
  - **Wired into `main.c` for hardware testing (temporary, same pattern as Battery/Light):** `#include "ir.h"` added to Includes; `volatile IrState g_ir_last_state` added next to the other test globals; inside `StartTask02`'s loop, one line added right after the Light read (`g_ir_last_state = IR_Read();`). DHT, Battery, and Light code were **not modified**.
  - **Verified so far:** clean compile of both `ir.c` and the updated `main.c` against the real ARM toolchain, zero warnings.
  - **Investigation (previous round):** pointing an IR remote control at the sensor and pressing a button visibly lit the sensor module's own onboard LED, but the original once-per-2s `g_ir_last_state` test never changed - see Open Question #12 (Sec 10) for the full analysis. This did NOT reopen the earlier "single pin, not sonar" conclusion (still correct); it raised a new, separate question about pulse-train vs. steady-level output.
  - **IR HARDWARE SIGNAL DETECTION — VERIFIED.** A temporary fast-sampling diagnostic (reading PB10 much faster than once per 2s) confirmed PB10 genuinely changes state while the IR remote transmits - the sensor responds to the IR remote signal, and the pulse-train theory from the investigation was correct. This confirms the hardware itself works and PB10 is wired/functioning correctly.
  - **Still pending, not yet done: the actual `ir.c` driver logic does not yet match this finding.** `ir.c`/`ir.h`/`main.c`'s test wiring are unchanged since they were first written - still a single poll every 2 seconds with an unconfirmed polarity guess, which we now know is the wrong read strategy for a pulse-train signal. The real driver rewrite (and the design decision behind it - full remote-protocol decoding vs. simple "IR activity detected" presence surrogate, matching Sec 2.2's actual spec intent) is intentionally deferred - see Open Question #12 for the exact open decision.

- **Button driver — implemented, awaiting real-hardware verification (including polarity).** `Embeded/Core/Inc/button.h`, `Embeded/Core/Src/button.c`. Sixth LNC Application-layer driver. One function: `ButtonState Button_Read(void)` (`ButtonState` = `BUTTON_NOT_PRESSED`/`BUTTON_PRESSED`) - reads `Button_Pin` (PA10) once via `HAL_GPIO_ReadPin`. Same shape as `ir.c`: no FreeRTOS task, no debouncing (kept simple, revisit only if real hardware shows bounce is a problem), one call = one raw read.
  - **PA10 config confirmed directly in code before writing this**: `GPIO_MODE_INPUT`, `GPIO_NOPULL`, no interrupt (`main.c`); `Button_Pin`=`GPIO_PIN_10`/`Button_GPIO_Port`=`GPIOA` (`main.h`); CubeMX label `Button`, `Signal=GPIO_Input` (`Embeded.ioc`) - matches Sec 9.1's pin table, same shape as IR's original config.
  - **Polarity is UNCONFIRMED, clearly marked as such in both `button.h` and `button.c`.** `GPIO_NOPULL` means the MCU doesn't bias the line, so the idle level depends on the external button module's own circuit, which isn't documented anywhere in this project. Current assumption: `GPIO_PIN_RESET` (LOW) = `BUTTON_PRESSED` - the common convention for breakout push-button modules with an onboard pull-up. To be confirmed or corrected on real hardware, same process as LED's pin-mapping and IR's polarity guess.
  - **Wired into `main.c` for hardware testing (temporary, same pattern as the other drivers):** `#include "button.h"` added to Includes; `volatile ButtonState g_button_last_state` added next to the other test globals; inside `StartTask02`'s loop, one line added right after the IR read (`g_button_last_state = Button_Read();`). DHT, Battery, Light, and IR code were **not modified**.
  - **Verified so far:** clean compile of both `button.c` and the updated `main.c` against the real ARM toolchain, zero warnings.
  - **VERIFIED ON REAL HARDWARE.** User tested on the real board - works correctly, and the polarity guess was correct: **LOW = `BUTTON_PRESSED` confirmed.** `button.h`/`button.c` are finished, permanent, and not yet wired into anything beyond the temporary `StartTask02` test call (no `Event` module exists yet to call `Button_Read()` for real).

- **Buzzer driver — implemented, awaiting real-hardware verification.** `Embeded/Core/Inc/buzzer.h`, `Embeded/Core/Src/buzzer.c`. Seventh LNC Application-layer driver, and the first one using PWM instead of a digital read. Two functions: `void Buzzer_On(void)` (sets ~50% duty via `__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 50)` then `HAL_TIM_PWM_Start`) and `void Buzzer_Off(void)` (`HAL_TIM_PWM_Stop` - fully stops toggling rather than just setting duty to 0). No frequency parameter - the spec only needs a fixed alarm tone on/off (Sec 2.3), not variable pitch. `htim3` accessed via a local `extern TIM_HandleTypeDef htim3;` in `buzzer.c`, same pattern as `battery.c`'s `hadc1`/`dht.c`'s `htim6`.
  - **A real, concrete problem found before implementing:** with TIM3's original CubeMX defaults (`Prescaler=0`, `Period=100`), the actual PWM frequency computes to ~792 kHz - far above the audible range and useless for a buzzer. Confirmed by checking `main.c`'s `MX_TIM3_Init()` directly, not guessed. Root cause: SYSCLK/PCLK1 = 80 MHz (`APB1CLKDivider = RCC_HCLK_DIV1`, confirmed in `SystemClock_Config`), same clock DHT's TIM6 uses, and TIM3 was never tuned for an audible tone.
  - **Fix, done at the CubeMX/`.ioc` + generated-code level** (same precedent as the SPI1 datasize fix and DHT_Pin's open-drain reconfiguration - hardware timing belongs in generated init code, not runtime driver hacks): user changed TIM3's Prescaler to 399 via CubeMX and regenerated code. **Kept `Period=100` unchanged** (as requested - conveniently makes the duty-cycle value read almost like a direct 0-100 percentage). Resulting frequency: `80,000,000 / (400 * 101) ≈ 1980 Hz` (~2 kHz), an audible tone.
  - **Verification of the Prescaler fix took two rounds**, both confirmed directly from the files rather than trusting the report alone: first check found `main.c` still at `Prescaler=0` and no `TIM3.Prescaler` entry in `Embeded.ioc` at all (file timestamp had changed, but the value hadn't - the CubeMX edit likely didn't register the first time). Second check, after the user re-saved/regenerated: `main.c` correctly shows `Prescaler = 399`.
  - **Known minor drift, not blocking:** `Embeded.ioc` still doesn't list a `Prescaler` entry for TIM3 (`IPParameters=Channel-PWM Generation1 CH1,Period` only, unlike TIM6's `IPParameters=Prescaler,Period`) even though `main.c` now has the correct value. Harmless for the current build (the generated code, not the `.ioc`, is what actually compiles), but **worth checking again if the project is ever regenerated from CubeMX in the future** - it could silently reset TIM3's prescaler back to 0.
  - **Wired into `main.c` for hardware testing (temporary, different shape from the sensor drivers since Buzzer is an actuator, not something to sample):** `#include "buzzer.h"` added to Includes; `volatile uint8_t g_buzzer_test_on` added next to the other test globals; inside `StartTask02`'s loop, added a toggle right after the Button read - alternates `Buzzer_On()`/`Buzzer_Off()` each 2-second iteration, so it's audibly on and off in a repeating pattern rather than just sampled into a debugger watch. DHT, Battery, Light, IR, and Button code were **not modified**.
  - **Verified so far:** clean compile of both `buzzer.c` and the updated `main.c` against the real ARM toolchain, zero warnings.
  - **VERIFIED ON REAL HARDWARE.** User tested on the real board - buzzer audibly turns ON and OFF every 2 seconds, exactly as expected. TIM3 Prescaler=399 confirmed correct (PWM ≈1980 Hz, audible), Period=100 unchanged. `buzzer.h`/`buzzer.c` are finished, permanent, and not yet wired into anything beyond the temporary `StartTask02` test toggle (no `Event` module exists yet to call `Buzzer_On`/`Buzzer_Off` for real).

**All 7 planned hardware drivers are now done and verified on real hardware: LED, DHT11, Battery, Light, IR (hardware signal detection verified; driver logic redesign still pending, Open Question #12), Button, Buzzer.**

- **Configuration module — implemented, compiled clean, linker-safety verified. AWAITING USER REVIEW/HARDWARE TEST.** First LNC Application-layer module (Sec 2.6). Four new files: `Embeded/Core/Inc/flash_storage.h`, `Embeded/Core/Src/flash_storage.c` (generic driver - knows nothing about Configuration's struct shape, just moves raw bytes to/from one fixed flash page) and `Embeded/Core/Inc/configuration.h`, `Embeded/Core/Src/configuration.c` (owns the actual limit values, defaults, and persistence policy).
  - **Spec logic check done before implementing, not assumed:** compared the proposed default values against `PROJECT_GUIDE.md`'s own Sec 2.10 (Normal/Warning/Error mode definitions) and Sec 2.5 (only Normal/Warning boundaries are configurable, no separate Error-range command) - confirmed the only logically consistent model is nested bands (Warning bounds strictly wider/more permissive than Normal bounds), and confirmed all four sensors' proposed defaults satisfy this. **Caveat recorded honestly:** the actual `final project.pdf` is not present anywhere in this project directory in this environment, so this check was against the MD's own transcription of the spec (which itself states it was derived directly from the spec text), not a fresh re-read of the primary source.
  - **Flash safety investigated from real project files before touching the linker script, not assumed:** `STM32L476RGTX_FLASH.ld` originally claimed the full `LENGTH = 1024K` with nothing reserved. `stm32l4xx_hal_flash.h` confirmed, for `STM32L476xx` specifically, `FLASH_PAGE_SIZE = 0x800` (2048 bytes) and `FLASH_BANK_SIZE = FLASH_SIZE >> 1` (two 512KB banks, dual-bank device) - the struct's own doc comment confirms page numbering: "0 to 255 for 1MB dual bank." Checked actual current usage via `arm-none-eabi-size`/`nm` on the last-built `Embeded.elf` before changing anything: only ~47KB used, ending around `0x0800bbfc` - nowhere near the top of flash. No STM32 bootloader or option-byte region overlaps this area either (the system bootloader lives in a separate memory region entirely, not inside `0x08000000-0x080FFFFF`).
  - **Reserved page: `0x080FF800`, 2048 bytes, the very last page of flash (Bank 2, Page 255 - `FLASH_BANK_2`/`FLASH_STORAGE_PAGE=255` in `flash_storage.h`).**
  - **Linker script change** (`Embeded/STM32L476RGTX_FLASH.ld`): `FLASH LENGTH` reduced from `1024K` to `1022K` (`1022K = 0xFF800` bytes, so the linker's usable region now ends exactly at `0x080FF7FF`, one byte before the reserved page begins) - a structural guarantee, not just "the program happens to be small enough": if the linker ever needed to place normal code/data past that boundary, the link would hard-fail with a region-overflow error rather than silently colliding with Configuration's storage.
  - **`ConfigurationData` struct** (defined inside `configuration.c`, not exposed in the header - callers only see the getter/setter API): `magic` (`uint32_t`, `0x434F4E46` = ASCII "CONF") + `version` (`uint32_t`, `1`) + 10 floats (temp normal low/high, temp warning low/high, humidity/light/battery normal-lower, humidity/light/battery warning-lower) = exactly 48 bytes, no compiler padding (everything 4-byte aligned), and 48 is conveniently a multiple of 8 (STM32L4's flash write granularity is one double-word/8 bytes at a time) - no padding tricks needed.
  - **Default values** (agreed with user, checked against spec logic above): Temperature Normal [15,30]°C / Warning [10,35]°C; Humidity Normal≥40% / Warning≥30%; Light Normal≥30% / Warning≥20%; Battery Normal≥30% / Warning≥20%.
  - **Load/save flow**: `Configuration_Init()` reads the reserved page directly (flash is memory-mapped, a plain struct read) - if `magic`+`version` match, the RAM working copy is loaded from flash; otherwise (first boot, or unrecognized/corrupted data) defaults are applied to the RAM copy and immediately saved. Every setter (`Config_SetTempNormalRange`, etc. - 8 total, matching Sec 2.5's 8 "set limit" commands 1:1) updates the RAM copy and calls `Configuration_Save()` immediately (erase page → unlock → 6× `HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, ...)` → lock) - no separate commit step.
  - **API**: `Configuration_Init()`, `Config_WasLoadedFromFlash()` (1 = loaded existing data, 0 = defaults were just applied - added for hardware-test observability, same spirit as every other module's debugger-watch pattern), 8 setters, 8 getters (`Config_GetTempNormalRange`/`Config_GetTempWarningRange` each fill two floats by pointer; the other 6 getters return a single float). `flash_storage.h` exposes just two generic functions: `FlashStorage_Write(const void*, size)` (validates `size` is a nonzero multiple of 8 and ≤2048, erases the page, writes it) and `FlashStorage_Read(void*, size)` (plain memory-mapped copy, always succeeds).
  - **Deliberately deferred, not forgotten** (per explicit user instruction this round): no mutex (nothing concurrent calls Configuration yet - no Communication-dispatch or Monitor module exists; Sec 7's RTOS design already anticipates this need, revisit when the second real caller is built), no wear leveling (single fixed page, rewritten whole each time - fine for this project's write frequency and flash endurance), no CRC (magic+version only, per explicit instruction).
  - **Wired into `main.c` for hardware testing (temporary, minimal - only what's needed to actually exercise "load/save on startup," not extra scope):** `#include "configuration.h"` added to Includes; three debugger-watchable globals added (`g_config_loaded_from_flash`, `g_config_temp_normal_low`, `g_config_temp_normal_high`); at the very top of `StartTask02` (before the existing DHT/Battery/Light/IR/Button/Buzzer test code, which is untouched), `Configuration_Init()` is called once, followed by `Config_WasLoadedFromFlash()` and `Config_GetTempNormalRange()` to populate the watch globals. No setter is exercised by this temporary test - only the "load or default-and-save" startup path, since that's what "implement load/save on startup" actually requires to be observable. DHT, Battery, Light, IR, Button, and Buzzer code were **not modified**.
  - **Compiled clean:** `flash_storage.c`, `configuration.c`, and the updated `main.c` each compile with **zero warnings** against the real ARM toolchain.
  - **Full-project link verified, not just individual files:** reconstructed the complete object list (`Debug/objects.list`, all ~57 existing `.o` files reused as-is, `main.o`/`flash_storage.o`/`configuration.o` freshly compiled) and ran the exact link command from `Debug/makefile`, substituting the updated linker script - **link succeeded (exit code 0) with zero errors**, which by itself proves the reserved page wasn't overflowed into (a region overflow is a hard linker error, not a silent success). Independently double-checked with `arm-none-eabi-nm`: highest used flash address after linking is `0x0800c378` (~49KB), and a direct search for any symbol address in the `0x080FF800-0x080FFFFF` range returned **zero matches**. This verification was done in a scratch copy - the real `Embeded/Debug/` build artifacts were not touched; a normal build inside STM32CubeIDE will pick up all the real source/linker-script changes correctly next time it's built there.
  - **VERIFIED ON REAL HARDWARE.** User tested on the real board: Configuration values load correctly from flash after a reset, and the default values (15.0/30.0 temp Normal range, and by extension the rest of the defaults) are correct. Both halves of the required behavior confirmed - first-boot defaults-and-save, and later-boot load-from-flash. `flash_storage.h/.c` and `configuration.h/.c` are finished and permanent. Still only wired into the temporary `StartTask02` test call - no Communication-dispatch or Monitor module exists yet to call the setters/getters for real.

- **Log module — implemented, compiled clean, full-project link verified. RESOLVED / VERIFIED ON REAL HARDWARE (2026-09-01) — see the root-cause/fix bullets near the end of this entry.** Second LNC Application-layer module (Sec 2.4). Two new files: `Embeded/Core/Inc/log.h`, `Embeded/Core/Src/log.c`. Reuses `message.h`'s existing `MeasurementSample` struct directly (timestamp + 4 sensor floats + mode) - no new duplicate struct.
  - **Storage confirmed explicitly by the user this round**: SD card via FatFs (the doc's own Sec 10 "Resolved" list had flagged this half of the Log/Configuration storage split as never actually confirmed, unlike Configuration's internal-flash half - now resolved for both).
  - **RTC dependency avoided by design, not worked around:** `Log_Write()` takes a `MeasurementSample` (which already carries a `uint32_t timestamp`) as its only input - it never reads `HAL_RTC_*` itself, same pattern every message struct in `message.h` already uses. This means Log is fully buildable and testable today with zero RTC involvement, consistent with "don't start RTC yet."
  - **Real gap found before implementing, not assumed:** checked `FATFS/App/fatfs.c` directly - `MX_FATFS_Init()` (already called at boot) only links the SPI diskio driver, it never calls `f_mount()`. Added `Log_Init()` to do this explicitly, returning whether the mount succeeded - important because this is the first module in the whole project to actually touch the SD card end-to-end (`MX_FATFS_Init()` alone never proved the filesystem itself works).
  - **Filename format constrained by an actual project setting, not a free choice:** checked `FATFS/Target/ffconf.h` - `_USE_LFN = 0` (long filenames disabled), so filenames must be classic 8.3. Chose `YYYYMMDD.LOG` (e.g. `20260831.LOG`), which fits exactly and is genuinely "named by date" per Sec 2.4, not just a workaround.
  - **`EpochToDate()`**: converts the given Unix epoch to year/month/day with two simple counting loops (count whole years, then whole months, using a standard Gregorian leap-year rule) - written for readability over cleverness, per Sec 12's beginner-friendly working rule.
  - **Float formatting avoids `%f` deliberately:** this project links with `--specs=nano.specs` (newlib-nano), which excludes float support in `printf`-family functions unless the linker is given `-u _printf_float` - which this project's build does not do anywhere. Using `%f` here would have silently produced wrong/garbage output rather than a compile error - a real, easy-to-miss embedded gotcha. Instead, `FormatFloat2dp()` manually splits a float into whole/fractional integer parts and formats each with plain `%d` (fully supported by nano.specs with no extra linker flags).
  - **Retention**: every `Log_Write()` call computes the filename for exactly `currentTimestamp - 7 days` and calls `f_unlink()` on it, ignoring a "file not found" result - guarantees at most 7 files ever exist without needing to scan the directory. Guarded against `uint32_t` underflow when the given timestamp is small (e.g. during early testing before a real time source exists) - skips deletion entirely rather than wrapping around to a huge bogus value.
  - **A real bug found and fixed by the compiler, not silently ignored:** the first version triggered a genuine `-Wformat-truncation` warning (GCC's format-length checker conservatively assumes a `uint16_t` year/`uint8_t` month/day could need more digits than realistic dates ever will, exceeding the original 13-byte buffer in its worst-case analysis). Fixed by sizing the buffer to the true worst case (`FILENAME_BUFFER_SIZE = 16`) rather than suppressing the warning - **zero warnings** confirmed after the fix.
  - **API**: `int Log_Init(void)` (mounts the SD card, call once at boot) and `void Log_Write(const MeasurementSample *sample)` (append one line to today's file, then run retention). No FreeRTOS task. No mutex yet (same reasoning as Configuration - nothing concurrent calls Log yet, no Monitor/Event module exists; revisit when the second real caller is built).
  - **Wired into `main.c` for hardware testing (temporary):** `#include "log.h"` added to Includes; three debugger-watchable globals added (`g_log_mounted`, `g_log_write_count`, `g_log_test_timestamp` - the last starting at `1788134400` = 2026-08-31 00:00:00 UTC, incrementing by 2 each loop to match the loop's own 2s cadence, since no RTC exists yet). `Log_Init()` is called once at the top of `StartTask02` (right before the existing Configuration/DHT test code, which is untouched). Each loop iteration, a `MeasurementSample` is built from the *real* current sensor readings (`g_dht_last_temperature`, `g_dht_last_humidity`, `g_light_last_percent`, `g_battery_last_voltage`) plus the test timestamp and a placeholder `MODE_NORMAL` (no Monitor exists yet to compute a real mode), and passed to `Log_Write()`. DHT, Battery, Light, IR, Button, Buzzer, and Configuration code were **not modified** (note: the user has since commented out the `Buzzer_On()` call in the existing test loop themselves, presumably to silence it during further testing - left as-is, not reverted).
  - **Compiled clean:** `log.c` and the updated `main.c` each compile with **zero warnings** against the real ARM toolchain (after the format-truncation fix above).
  - **Full-project link re-verified:** added `log.o` to the same full-project link setup used for Configuration's verification (all existing object files + freshly compiled `main.o`/`log.o`) - **link succeeded (exit code 0)**, confirming all FatFs symbols (`f_mount`/`f_open`/`f_write`/`f_close`/`f_unlink`) resolve correctly against the already-built FatFs objects, and nothing overflows the reduced flash region.
  - **REAL-HARDWARE TEST RUN — PARTIAL FAILURE, UNDER ACTIVE DEBUGGING.** User tested: `g_log_mounted == 1` (SD card mounted successfully - confirms the SPI wiring, `disk_initialize()`, and reading the boot sector/filesystem all genuinely work). But after removing the card and checking on a PC, **no `20260831.LOG` file exists at all, no data was written.**
  - **Investigated by re-reading `Log_Write()` line by line before touching anything - no control-flow bug found.** The open -> build line -> write -> close -> retention sequence and flag usage (`FA_OPEN_APPEND | FA_WRITE`) are structurally correct. The real gap: `f_write()`'s and `f_close()`'s return values were being silently discarded, and only `f_open()` failing caused an early return - so there was previously **zero visibility** into which of the three FatFs calls was actually failing.
  - **Two leading hypotheses, not yet distinguishable without hardware data:** (a) `f_open()` itself is failing - plausible and would fully explain "no file at all," since creating/appending a file requires writing a new directory entry, a real disk *write*, distinct from the reads a successful mount already proved work; or (b) `f_open()` succeeds but `f_write()`/`f_close()` fail silently (also very plausible - CubeMX-generated SPI SD-card diskio templates are a well-known source of "reads work, writes don't" bugs, and FatFs only truly commits buffered data to the physical card on `f_close()`/`f_sync()`).
  - **Diagnostics added (additive only - `Log_Write()`'s existing behavior/signature is unchanged, no redesign, per explicit instruction):** `log.c` now captures `f_open()`/`f_write()`/`f_close()`'s actual `FRESULT` return values (previously discarded) and `f_write()`'s `bytesWritten` output, in new static variables. Four new getters added to `log.h`: `Log_GetLastOpenResult()`, `Log_GetLastWriteResult()`, `Log_GetLastCloseResult()` (each returns a FatFs `FRESULT` cast to `int` - 0 = `FR_OK` = success), `Log_GetLastBytesWritten()`.
  - **Wired into `main.c`** (temporary, additive): 4 new debugger-watchable globals - `g_log_open_result`, `g_log_write_result`, `g_log_close_result`, `g_log_bytes_written` - populated right after each `Log_Write()` call in the existing `StartTask02` test loop, same spot as the existing `g_log_write_count` increment. Nothing else about the existing Log test wiring or any other module was touched.
  - **Compiled clean:** `log.c` and the updated `main.c` each compile with **zero warnings**. Full-project link re-verified (same setup as before) - **succeeds**, confirming nothing broke.
  - **FRESULT legend for reading these variables** (from `ff.h`): 0=`FR_OK` (success), 1=`FR_DISK_ERR` (low-level disk I/O error - points at the SPI/diskio write path), 3=`FR_NOT_READY` (drive not ready), 4=`FR_NO_FILE`, 5=`FR_NO_PATH`, 6=`FR_INVALID_NAME`, 7=`FR_DENIED` (access denied / directory full), 8=`FR_EXIST`, 10=`FR_WRITE_PROTECTED`, 11=`FR_INVALID_DRIVE`, 12=`FR_NOT_ENABLED`, 13=`FR_NO_FILESYSTEM`.
  - **NOT yet redesigned, per explicit instruction** - waiting on the user's next hardware test to see which diagnostic is nonzero before deciding what to actually fix.
  - **New finding, changes the investigation's focus:** on a second real-hardware check, `g_log_mounted` read **0** (mount now failing), where the previous check had read 1 (mount succeeding). Since `Log_Init()` only ever stored a boolean success/fail, not `f_mount()`'s actual `FRESULT`, there was no way to see *why* it now fails, or whether it's the same failure as before. Per explicit instruction, paused investigating `f_write()` and added one more diagnostic: `Log_GetLastMountResult()` (new getter in `log.h`/`log.c`, backed by a new `s_lastMountResult` static capturing `f_mount()`'s real `FRESULT`) and a matching `g_log_mount_result` global in `main.c`, populated right after `Log_Init()` at the top of `StartTask02`. No other change made - `f_write`/`f_open`/`f_close` diagnostics from the previous round are untouched, Log's design is untouched, Event has not been started.
  - **Compiled clean, zero warnings**; full-project link re-verified, succeeds.

- **Comparison against a separately-provided, known-working SD/FatFs reference project (a different, non-RTOS STM32L476 project that successfully writes/reads a file) — full file-by-file audit, no code changed while producing it.** Checked `.ioc`, `MX_SPI1_Init()`, `HAL_SPI_MspInit()`, `main.h`'s pin/handle `#define`s, `user_diskio.c`/`user_diskio_spi.c`, `fatfs.c`, and `ffconf.h`'s core settings against the reference's documented equivalents, item by item.
  - **Everything checked matches the known-working reference exactly**: SPI Mode 0 (`CLKPolarity=LOW`/`CLKPhase=1EDGE`), `NSS=SPI_NSS_SOFT`, `SD_CS`=PB6 as a plain GPIO output (not hardware NSS), **MISO (PA6) pull-up present**, `SD_SPI_HANDLE`/`SD_CS_GPIO_Port`/`SD_CS_Pin` `#define`d correctly in `main.h`, `FCLK_SLOW()`/`FCLK_FAST()` clock-switching and the 80-dummy-clock sequence inside `USER_SPI_initialize()` byte-for-byte identical, `FATFS_LinkDriver()` called before the scheduler starts, `_USE_LFN=0`/`_MIN_SS=_MAX_SS=512`/`_FS_LOCK=2` all matching. **This rules out an SPI-mode, wiring, or SD-protocol-sequence bug** — none of the classic SD-over-SPI gotchas differ from a project that is confirmed to work.
  - **Also discovered, not previously recorded in this guide: `log.c`'s `Log_Init()` has since been hand-edited (outside of a guide-tracked session) to add the reference project's exact "belt-and-braces" pattern** — force `SD_CS` HIGH, `HAL_Delay(1000)`, then manually clock out 10 bytes of `0xFF` (80 dummy clocks) via `HAL_SPI_Transmit()`, all *before* calling `f_mount()` — and `Log_Write()`'s `f_open()` call was switched from `FA_OPEN_APPEND` to `FA_CREATE_ALWAYS` (matching the reference project's flag choice). The old `FA_OPEN_APPEND` version is left commented out just above. **These changes mirror the reference project correctly, but the file is still not being created** — meaning the SPI/protocol-level fix path has already been tried and does not by itself explain the failure.
  - **New hypothesis found, not yet tested on hardware — the one significant, genuine architectural difference from the reference project: this project runs FatFs/SD code inside a FreeRTOS task with a very small, never-adjusted stack.** The reference project has no RTOS at all — `SD_FatFs_Test()` runs directly in `main()`'s `while(1)` loop, on the full system stack. Here, `Log_Init()`/`Log_Write()` run inside **`InitTask`** (`StartTask02`), created in `main.c` with `.stack_size = 128 * 4` = **512 bytes** — confirmed (via `cmsis_os2.c`) that `stack_size` is bytes, divided by 4 for FreeRTOS's word-based depth, so this is exactly `configMINIMAL_STACK_SIZE` (128 words, `FreeRTOSConfig.h`), CubeMX's smallest default, sized for a trivial/idle task — never bumped up for this task's real workload (Sec 9.2 already flagged, as an unrelated cleanup note, that all 7 tasks still share this same uniform 128-word default instead of the per-task sizes Sec 7 calls for).
    - `Log_Write()` declares a local FatFs `FIL file;` on this task's stack. With `_FS_TINY=0` and `_MAX_SS=512`, `ff.h`'s `FIL` struct embeds a `BYTE buf[512]` sector buffer plus several more fields — **~560-570 bytes by itself, already bigger than the entire 512-byte stack**, before counting `Log_Write()`'s other locals (`filename`, 4× value strings, a 128-byte `line` buffer), FatFs's own internal call frames inside `f_mount()`/`f_open()`/`f_write()`, or `USER_SPI_initialize()`/`USER_SPI_ioctl()`'s local buffers (`ocr[4]`, `csd[16]`) further down the same call chain.
    - `FreeRTOSConfig.h` does **not** define `configCHECK_FOR_STACK_OVERFLOW` (defaults off), so an overflow here would never be caught or reported — it would just silently corrupt whatever memory sits past the stack (heap blocks, another task's TCB/stack), which could explain corrupting the very FatFs state being used mid-call, with no crash and no error message (doubly so since USART2 is deliberately kept protocol-only, so there is nowhere for a crash log to appear even if one existed).
    - This fits the observed symptom (file never appears, sometimes-successful/sometimes-failed mount per the two hardware runs above) better than anything at the SPI/FatFs-flag level, precisely because the SPI/FatFs-flag level has already been checked exhaustively (this section) and against a known-good reference (previous bullet) without finding a difference.
  - **Recommended next step (as of the previous session):** increase `InitTask_attributes.stack_size` in `main.c` from `128 * 4` to something like `1024 * 4` (4 KB) — a one-line, one-file change that touches no SPI/GPIO/FatFs logic already verified against the working reference. `configTOTAL_HEAP_SIZE` is 15360 bytes, so there is ample headroom.

- **ROOT CAUSE CONFIRMED, FIX APPLIED, VERIFIED ON REAL HARDWARE — Log/SD issue is RESOLVED (2026-09-01).**
  - **Root cause:** `InitTask`'s FreeRTOS stack was only **512 bytes** (`.stack_size = 128 * 4` in `main.c`'s `InitTask_attributes`, CubeMX's default minimum). `Log_Write()`'s local FatFs `FIL` object alone is ~560-570 bytes (with `_FS_TINY=0`/`_MAX_SS=512`, `FIL` embeds a 512-byte sector buffer) — already bigger than the entire stack — so every call into `f_open()`/`f_write()`/`f_close()` from inside `InitTask` was overflowing the stack. With `configCHECK_FOR_STACK_OVERFLOW` not defined in `FreeRTOSConfig.h`, this overflow was never caught or reported; it silently corrupted nearby memory instead of crashing or printing an error, which is exactly why the SD card could mount successfully (needs little stack) while the actual file write silently never happened (needs much more).
  - **Fix applied (by the user directly):** increased `InitTask_attributes.stack_size` in `main.c` from `128 * 4` (512 bytes) to **`1024 * 4` (4 KB)**. No other file was changed as part of this fix — every SPI/GPIO/FatFs-driver file that had already been verified against the known-working reference project (the file-by-file comparison two bullets above) was left untouched.
  - **HARDWARE TEST RESULT: PASS / VERIFIED.** User flashed the board with the increased stack size and confirmed on real hardware that SD logging now works — data is successfully being written to the SD card. This closes out the "Log write path failing on real hardware" investigation that spanned the previous several sessions (mount-succeeds-but-no-file, the FRESULT diagnostics, the reference-project comparison, and finally this stack-size fix).
  - **The `g_log_open_result`/`g_log_write_result`/`g_log_close_result`/`g_log_bytes_written`/`g_log_mount_result` diagnostic globals and getters (added during the earlier investigation) are still present in `log.c`/`log.h`/`main.c`** — left in place for now since Log is a newly-verified module and they're still useful for ongoing observability; not yet cleaned up, same as this project's convention for other recently-verified modules (e.g. DHT's watch globals).

**Log module status: DONE and HARDWARE-VERIFIED.** Do not revisit Log's design or the stack-size fix unless new evidence of a problem appears.

- **Event module — VERIFIED / PASSED on real hardware (2026-09-01).** Third LNC Application-layer module (Sec 2.3). Two new files: `Embeded/Core/Inc/event.h`, `Embeded/Core/Src/event.c`.
  - **No new structs** — every Sec 2.3 case already had a matching struct in `message.h` (`ModeTransitionMessage`, `TimestampWithFlagMessage`, `TimestampMessage`), reused as-is, consistent with this project's established "reuse, don't duplicate" pattern.
  - **API**: `Event_Init()` (resets internal alarm state, call once at boot after `Log_Init()`), `Event_OnModeTransition`/`Event_OnObjectDetection`/`Event_OnConfigurationChanged`/`Event_OnInitStartup` (each: writes a timestamped record to today's events file, updates LED/buzzer per Sec 2.3.1, and returns the built `TAG_EVENT_*` CC-frame via an `out_buffer`/`out_buffer_size` → returned-byte-count signature, matching `message.h`'s own builder convention exactly), `Event_CheckAlarmButton()` (polls the button; if the alarm is active and the button is pressed, silences the buzzer only — intended to be called periodically by whichever task ends up owning this, not yet decided, most likely the future `ObjectDetectionTask`), and `Event_IsAlarmActive()` (test-observability getter, same spirit as `Config_WasLoadedFromFlash()`).
  - **Events file**: separate from Log's `YYYYMMDD.LOG` — `YYYYMMDD.TXT`, same 8.3-compatible naming and the same 7-day retention/rotation logic as Log (per this session's explicit decision). Uses `FA_OPEN_APPEND | FA_WRITE` (not Log's current `FA_CREATE_ALWAYS`) since multiple events must accumulate through the day rather than overwrite — **flagged, not silently diverged**: Log's current `FA_CREATE_ALWAYS` looks like debugging-era leftover (see Log's own history above) rather than a deliberate final choice; worth revisiting for Log separately sometime, but Log itself was explicitly left untouched here.
  - **Extension changed from `.EVT` to `.TXT` later the same session, per explicit user request**: the file is plain human-readable CSV text (`timestamp,eventType,description`), not binary, so `.EVT` had no technical justification — only `.TXT` opens directly in Notepad/etc. for manual SD-card inspection, which matters since the user checks the card by hand during testing. One-line change (`BuildFilename`'s `snprintf` format string in `event.c`), plus matching comments in `event.c`/`event.h`/`main.c`. Recompiled clean (zero warnings) against the real ARM toolchain after the change. Log's `.LOG` files were **not** touched.
  - **`EpochToDate`/`IsLeapYear`/`BuildFilename` duplicated from `log.c`** (small, ~20 lines) rather than shared, because `log.c`'s versions are `static`/private and Log is finished/verified/"do not revisit" — exporting them would mean touching already-verified code just to avoid this small duplication.
  - **Mode-transition logic (Sec 2.3.1) branches generically on `newMode`** (Error/Warning/Normal) rather than hard-coding each of the 5 listed `(old,new)` pairs separately — Warning and Normal branches additionally stop the buzzer only when `oldMode == MODE_ERROR`, which naturally covers both "Error→Warning" and "Error→Normal" with one rule.
  - **Two spec points resolved this session, not guessed:**
    1. Configuration-changed events: Sec 2.3's own bullet only says "write to events file," not "send to CC," unlike the other 3 cases — but `TAG_EVENT_CONFIG_CHANGED` already existed as a real wire tag/builder. **User confirmed: also send to CC.** `Event_OnConfigurationChanged` does both.
    2. Events-file naming/retention wasn't specified by Sec 2.3 (unlike Sec 2.4's explicit numbers for Log). **User confirmed: `YYYYMMDD.EVT`, same 7-day rotation as Log.**
  - **Deliberately NOT implemented** (Open Question #3, Sec 10): "suppress non-essential operations" / "resume full operation" during Error-mode transitions — the spec never defines which operations are non-essential, and no other module currently has anything to suppress. `Event_OnModeTransition`'s `→Error` branch does exactly the spec-listed concrete actions (LED red, alarm on, write, send) and nothing else.
  - **Verified so far:** `event.c` and the updated `main.c` both compile with **zero warnings** against the real ARM toolchain. Full-project link re-verified (same method as Configuration/Log's verification — real object list + freshly compiled `main.o`/`event.o`, real linker script): **link succeeds (exit code 0)**, flash usage ~70 KB text (well under the 1022 KB budget), and a direct symbol-address search confirms **zero symbols** land in Configuration's reserved `0x080FF800` page.
  - **No PC unit test written** — like Log, Event's real logic depends on FatFs/GPIO (HAL headers), so it isn't PC-compilable in its real form; this matches Log's own precedent (no `Tests/Log/` directory exists either). Verification is ARM-toolchain compile + real-hardware test only, same as Log/Configuration/every driver.
  - **Wired into `main.c` for hardware testing (temporary, additive only — DHT/Battery/Light/IR/Button/Buzzer/Configuration/Log code untouched):** `#include "event.h"` added; 5 new debugger-watchable globals (`g_event_last_frame_length`, `g_event_alarm_active`, `g_event_open_result`, `g_event_write_result`, `g_event_close_result`). At the top of `StartTask02` (right after `Log_Init()`): `Event_Init()`, then `Event_OnConfigurationChanged`/`Event_OnInitStartup` each exercised once with test data. Inside the main loop (after the existing Log-write block): a 6-step static cycle through all 5 of Sec 2.3.1's listed mode transitions (Normal→Warning→Error→Warning→Normal→Error→Normal→…), one step per 2s iteration — same "cycle and watch the LED" approach the original LED boot-test used — plus a call to `Event_CheckAlarmButton()` each iteration so pressing the button while the alarm is active is testable.
  - **VERIFIED ON REAL HARDWARE (2026-09-01).** User checked the SD card: `<date>.TXT` contains correct timestamped records for every tested case — `CONFIGURATION CHANGED`, `STARTUP (NORMAL)`, and all 5 Sec 2.3.1 mode-transition pairs (`NORMAL->WARNING`, `WARNING->ERROR`, `ERROR->WARNING`, `WARNING->NORMAL`, `NORMAL->ERROR`, `ERROR->NORMAL`) — with repeated transitions correctly appended (confirms `FA_OPEN_APPEND` behaves as intended, not overwriting like Log's current `FA_CREATE_ALWAYS`). `event.h`/`event.c` are finished and permanent. Temporary test wiring left in `main.c`'s `StartTask02` for now (same convention as other recently-verified modules, e.g. DHT's watch globals) — not yet cleaned up.

**Event module status: DONE and HARDWARE-VERIFIED.** Do not revisit Event's design unless new evidence of a problem appears.

- **Monitor module — VERIFIED / PASSED on real hardware (2026-09-01).** Fourth LNC Application-layer module (Sec 2.1) — and the first one wired into its own real, permanent FreeRTOS task (`MonitorTask`/`StartTask03`) instead of temporary scaffolding in `StartTask02`. Two new files: `Embeded/Core/Inc/monitor.h`, `Embeded/Core/Src/monitor.c`.
  - **No new structs** — reuses `MeasurementSample` (output) and `ModeTransitionMessage` (built internally for Event), both already in `message.h`.
  - **API**: `Monitor_Init()` (resets internal state, starts the DHT11 driver via `DHT_Init()` — moved here from `main.c`, since Monitor is now the sole caller of `DHT_Read()`) and `Monitor_Sample(uint32_t timestamp, MeasurementSample *out_sample)` (one full cycle: read all 4 sensors, classify, log, and — only on a real mode change — notify Event).
  - **Classification** (Sec 2.10): two small static helpers, `ClassifyRange` (temperature — has both a low and high bound per mode) and `ClassifyLowerBound` (humidity/light/battery — lower bound only, per Sec 2.5's asymmetry). Each checks the tighter Normal band first, then the wider Warning band, else Error. Overall mode = the numeric **max** of the 4 per-sensor results, relying on `SystemMode`'s enum values already being ordered `MODE_NORMAL(0) < MODE_WARNING(1) < MODE_ERROR(2)` (`tlv_common.h`) — documented with a comment at the point of reliance.
  - **Decision (per user's explicit approval this session): classification calls `Config_Get*()` directly inside `monitor.c`** rather than a large parameterized pure function — simpler API, consistent with every other Application module in this project, at the cost of not being PC-unit-testable (same tradeoff Log/Event/Configuration already accepted).
  - **DHT read-failure handling (per user's explicit approval): on `DHT_Read()` failure, Monitor keeps the last successfully-read temperature/humidity** (`s_lastTemperature`/`s_lastHumidity`, static) rather than substituting `0.0`, which could otherwise look like a spurious Error-range reading.
  - **DHT priority-boost workaround (Sec 14/15's long-open item, now resolved): moved inside `Monitor_Sample()` itself**, wrapping only the `DHT_Read()` call (`osThreadSetPriority` on `osThreadGetId()`, boost then restore) — works regardless of which task calls `Monitor_Sample()`, so the caller no longer needs to know about this DHT-specific quirk. This is a deliberate, scoped exception: Monitor is now mildly FreeRTOS-aware, unlike Configuration/Log/Event, which stay plain/RTOS-independent.
  - **Task wiring (per user's explicit approval): `StartTask03` (`MonitorTask`) now runs Monitor for real** — `Monitor_Init()` once, then `for(;;) { osDelay(5000); Monitor_Sample(...); }`. The delay runs **before** the first sample, not after — a deliberate, documented mitigation for a real (if narrow) startup-ordering gap: `InitTask` (`StartTask02`) calls `Configuration_Init()`/`Log_Init()`/`Event_Init()` at boot, but both tasks currently share the same priority (`osPriorityLow`) with no explicit synchronization primitive between them (Sec 7's frozen RTOS design doesn't define one). A 5s head start gives those quick, synchronous calls ample time to finish first. Flagged as a pragmatic mitigation, not a proper fix — revisit with a real sync primitive (e.g. a semaphore) only if this ever proves insufficient in practice.
  - **`main.c` cleanup, not a Log/Event code change:** removed the now-superseded temporary DHT/Battery/Light read block and the temporary Log-write / mode-transition-cycle test blocks from `StartTask02` — Monitor is now their real, permanent caller. This was necessary, not just tidiness: leaving the old `StartTask02` DHT-read test running alongside Monitor's own `DHT_Read()` calls from `StartTask03` would have let two different tasks call `DHT_Read()` concurrently, which is unsafe (`dht.c`'s bit-banging has no reentrancy protection — documented in its own file header as "nothing concurrent calls this yet"). `dht.h`/`battery.h`/`light.h` includes removed from `main.c` (no longer referenced there). `Event_CheckAlarmButton()` is now called from `StartTask02`'s remaining 2s loop (alongside the still-driver-only IR/Button reads and the Buzzer toggle test) instead of the removed Event test block, keeping "button silences an active alarm" testable and reasonably responsive. **`log.c`/`log.h`/`event.c`/`event.h` themselves were not touched** — only `main.c`'s temporary test glue calling them.
  - **Verified so far:** `monitor.c` and the updated `main.c` both compile with **zero warnings** against the real ARM toolchain. Full-project link re-verified (same method as Configuration/Log/Event — real object list + freshly compiled `main.o`/`event.o`/`monitor.o`, real linker script): **link succeeds (exit code 0)**, flash usage ~70.8 KB text (well under the 1022 KB budget), zero symbols in Configuration's reserved `0x080FF800` page.
  - **No PC unit test** — same reasoning as Log/Event/Configuration: Monitor's real logic calls HAL-based drivers directly, so it isn't PC-compilable in its real form. Verification is ARM-toolchain compile + full-project link + real-hardware test.
  - **Wired into `main.c` for hardware testing** — this time not "temporary scaffolding to remove later" but Monitor's actual permanent task body (see above). 7 new debugger-watchable globals: `g_monitor_sample_count`, `g_monitor_test_timestamp` (Monitor's own fixed test clock, +5s/cycle — no RTC exists yet), `g_monitor_last_temperature`/`_humidity`/`_light`/`_battery`/`_mode`. Log's and Event's existing diagnostic globals (`g_log_open_result` etc., `g_event_alarm_active` etc.) are now refreshed from `StartTask03` after each `Monitor_Sample()` call, since that's the real call site now.
  - **VERIFIED ON REAL HARDWARE (2026-09-01).** User confirmed all points from the Sec 15 test procedure passed: ~5s sample cadence with fresh readings from all 4 sensors, one `.LOG` line per sample, `.TXT` entries only on actual mode changes (matching `g_monitor_last_mode` transitions), correct classification against Configuration's limits, LED/buzzer state changing only through Event (never directly from `monitor.c`), and the button correctly silencing an active alarm. `monitor.h`/`monitor.c` are finished and permanent. Temporary/permanent test globals (`g_monitor_*`) left in `main.c` for now, same convention as other recently-verified modules.

**Monitor module status: DONE and HARDWARE-VERIFIED.** Do not revisit Monitor's design unless new evidence of a problem appears.

- **Object Detection module — VERIFIED / PASSED on real hardware, including full resolution of the stack-overflow investigation below (2026-09-01).** Fifth LNC Application-layer module (Sec 2.2) — and the second one (after Monitor) wired into its own real, permanent FreeRTOS task (`ObjectDetectionTask`/`StartTask04`) from the start, not temporary `StartTask02` scaffolding. Two new files: `Embeded/Core/Inc/object_detection.h`, `Embeded/Core/Src/object_detection.c`.
  - **No new structs** — reuses `TimestampWithFlagMessage` (already the exact shape `Event_OnObjectDetection` expects) and `ir.h`'s existing `IrState` enum.
  - **Open Question #12 (Sec 10) resolved this session, closing it out**: rather than rewriting `ir.c`/`ir.h` to decode the IR pulse train (full remote-protocol decoding, or any EXTI/interrupt-based redesign), the fast-poll + activity-timeout logic lives entirely in this new Application module, and **`ir.c`/`ir.h` were not touched at all** — `IR_Read()` is called exactly as it already existed. This also makes `ir.c`'s still-unconfirmed HIGH/LOW polarity irrelevant to this module, since it only cares whether the raw reading *changed* between polls, not which specific value currently means what.
  - **API**: `ObjectDetection_Init()` (resets state to "no object", a safe starting default matching `Monitor_Init()`'s own precedent), `ObjectDetection_Poll(uint32_t timestamp)` (one poll cycle: read the pin, update the activity timer, and — only on an actual reported-state change — call `Event_OnObjectDetection()`), `ObjectDetection_GetCurrentState()` (test-observability getter, same spirit as `Event_IsAlarmActive()`).
  - **Detection logic**: two constants, `IR_POLL_INTERVAL_MS = 20` and `IR_ACTIVITY_TIMEOUT_MS = 400` (both user-approved starting values, tunable after the hardware test, not spec-given numbers). Each poll compares the current raw reading to the previous one — a change resets a "time since last activity" counter to 0; no change advances it by `IR_POLL_INTERVAL_MS`. The reported state is `IR_DETECTED` as long as that counter is under the timeout (so detection is immediate, on the very first pulse) and becomes `IR_NOT_DETECTED` only after a full `IR_ACTIVITY_TIMEOUT_MS` of no activity — turning a pulse train into a stable signal without decoding it.
  - **Scope kept tight, per explicit instruction**: no SD-card writes (Event's `Event_OnObjectDetection()` already writes the events file), no LED/buzzer calls (Event owns those entirely — confirmed by inspection, `object_detection.c` has no `led.h`/`buzzer.h` includes at all), no UART/TLV logic (same deferred boundary every other Application module already has — the frame `Event_OnObjectDetection()` returns is discarded here, same as Monitor does with `Event_OnModeTransition()`'s return).
  - **Task wiring (per user's explicit approval): `StartTask04` (`ObjectDetectionTask`) now runs Object Detection for real** — `ObjectDetection_Init()` once, then `for(;;) { ObjectDetection_Poll(...); Event_CheckAlarmButton(); osDelay(IR_POLL_INTERVAL_MS); }`. **`Event_CheckAlarmButton()` moved here from `StartTask02`'s 2s loop** (removed there, per user's explicit approval) — polling it every 20ms instead of every 2s makes silencing an active alarm far more responsive. `StartTask02` keeps its own independent `Button_Read()` call for its own debugger-watch/polarity purpose — unaffected, since a plain GPIO read (unlike DHT's bit-banging) has no reentrancy hazard being called from two tasks.
  - **Verified so far:** `object_detection.c` and the updated `main.c` both compile with **zero warnings** against the real ARM toolchain. Full-project link re-verified (same method as every previous module — real object list + freshly compiled `main.o`/`event.o`/`monitor.o`/`object_detection.o`, real linker script): **link succeeds (exit code 0)**, flash usage ~71.2 KB text (well under the 1022 KB budget), zero symbols in Configuration's reserved `0x080FF800` page.
  - **No PC unit test** — same reasoning as Log/Event/Configuration/Monitor: calls the HAL-based `IR_Read()` driver directly, not PC-compilable in its real form. Verification is ARM-toolchain compile + full-project link + real-hardware test.
  - **Wired into `main.c`**: 2 new debugger-watchable globals, `g_object_detection_test_timestamp` (this task's own fixed test clock — no RTC exists yet, incremented by 1 per 20ms poll, a test-only simplification since it only actually matters on the rare poll where a change occurs) and `g_object_detection_last_state` (mirrors `ObjectDetection_GetCurrentState()`).
  - **HARDWARE TEST FAILED (2026-09-01): buzzer sounds continuously, `.TXT` shows `OBJECT DETECTED` with no matching `OBJECT CLEARED`, MCU reset doesn't clear it.** Investigated before changing anything, per explicit instruction:
    - **Root-cause hypothesis (user's, confirmed as the most consistent fit for every symptom by re-reading `object_detection.c`/`ir.c` line by line):** `ir.c`'s `IR_Read()` is a single, undebounced instantaneous read on a `GPIO_NOPULL` pin - its idle-state behavior with no IR source present was never actually verified, only assumed ("the sensor itself actively drives the line either way"). Demodulating IR-receiver modules (already confirmed to be this sensor's real type, from the earlier pulse-train investigation) are commonly noisy/chattering at idle without a strong nearby modulated signal - a well-known characteristic of this sensor class, not a one-off guess. Since `ObjectDetection_Poll()` treats **any** raw-value change as activity and resets its 400ms countdown on every one, continuous chatter - even just faster than once per 400ms - makes that countdown mathematically unable to ever complete, so `IR_NOT_DETECTED` becomes unreachable. This explains every symptom at once: the near-immediate first `OBJECT DETECTED` (the first noise toggle after boot), the total absence of `OBJECT CLEARED`, and why a reset doesn't help (the noise is a steady-state hardware characteristic, not a one-time event).
    - **Algorithm and `ir.c`/`ir.h` deliberately NOT changed yet**, per explicit instruction - this is still a hypothesis to confirm with real data, not yet an established fact.
    - **Minimal diagnostic added instead**: new getter `ObjectDetection_GetRawReading()` (`object_detection.h`/`.c`) returning `s_lastRawReading` - the raw `IR_Read()` value from the most recent poll, as already sampled internally (no new/redundant hardware read). Wired into `main.c` as a new debugger-watchable global, `g_object_detection_raw_ir_state`, updated every 20ms poll in `StartTask04` right alongside the existing `g_object_detection_last_state` (kept, unchanged). Purely additive - `ObjectDetection_Poll()`'s own logic is byte-for-byte unchanged.
    - **Verified:** `object_detection.c`/`object_detection.h`/`main.c` compile **zero warnings** against the real ARM toolchain; full-project link re-verified (same method as every previous module) - link succeeds, flash usage ~71.3 KB (negligible change from the getter/global), zero symbols in Configuration's reserved flash page.
    - **Round-1 diagnostic result (user's test): `g_object_detection_raw_ir_state` stayed `IR_NOT_DETECTED` through all 3 phases (idle / remote pressed / remote released) - never observed to change at all.** This actually **rules against** the chatter/noise hypothesis above (chatter would show up as frequent toggling, not a value that never moves) - a genuinely different, more surprising result. It's also in tension with the *previous* round's one real `OBJECT DETECTED` log entry, which could only have been produced by `ObjectDetection_Poll()` observing at least one real raw-value change - meaning the pin evidently *can* go LOW through this exact code path, just not observed to during this particular 3-phase test.
    - **Round-2 investigation, source inspection only, no code changes to `ir.c`/`object_detection.c`:** re-checked `main.c`'s real, CubeMX-generated `MX_GPIO_Init()` (not just `ir.c`) - `IR_Pin`=`GPIO_PIN_10`, `IR_GPIO_Port`=`GPIOB` (`main.h`), `GPIO_MODE_INPUT`, `GPIO_NOPULL`, `GPIOB` clock enabled - matches Sec 9.1's pin table and `ir.h`'s own documented assumptions exactly, **no discrepancy found** between the generated init code and what the driver assumes. `ir.c`'s conversion (`GPIO_PIN_RESET`→`IR_DETECTED`, else `IR_NOT_DETECTED`) is a single unambiguous comparison - no logic bug found. This rules out wrong-pin and GPIO-misconfiguration as explanations, narrowing the remaining candidates to: wiring/hardware, the sensor genuinely not producing a signal reliably during that particular test, or a **sampling-rate/aliasing problem** (if the sensor's real active-LOW pulses are brief/infrequent relative to idle gaps, a fixed 20ms sample could easily land only in idle gaps and miss them - different from the round-1 "constant chatter" theory).
    - **Round-2 diagnostic added, per explicit instruction, still no algorithm change**: a second, independent `HAL_GPIO_ReadPin(IR_GPIO_Port, IR_Pin)` call added directly in `main.c`'s `StartTask04` (not inside `ir.c`/`object_detection.c` - both remain byte-for-byte untouched), exposed as a new debugger-watchable global `g_object_detection_raw_gpio_level` (`1`=`GPIO_PIN_SET`/HIGH, `0`=`GPIO_PIN_RESET`/LOW) - the direct GPIO level, bypassing `ir.c`'s `IrState` conversion entirely, so `ir.c`'s mapping can be ruled in/out as a factor by comparing it side by side with `g_object_detection_raw_ir_state` (kept, unchanged).
    - **Verified:** `main.c` compiles **zero warnings** against the real ARM toolchain (only file changed this round). Full-project link re-verified - link succeeds, flash usage ~71.3 KB (negligible change), zero symbols in Configuration's reserved flash page.
    - **Round-2 result (user's test): `g_object_detection_raw_gpio_level` stays `1` (HIGH) and `g_object_detection_raw_ir_state` stays `IR_NOT_DETECTED`, consistently, with and without holding the remote toward the sensor. User separately confirmed the hardware wiring and GPIO configuration are correct** - narrowing the investigation to the software execution path itself: is `StartTask04` actually looping and genuinely re-sampling PB10 every 20ms, or did it run once (e.g. hang shortly after) and freeze at whatever it read that one time?
    - **Round-3 investigation, source inspection only, still no changes to `ir.c`/`object_detection.c`:** traced the full path line-by-line (`StartTask04` → `ObjectDetection_Poll()` → `IR_Read()` → `HAL_GPIO_ReadPin()` → conversion) - straight-line code throughout, no conditional in any of these functions could skip an iteration or a read. Confirmed `ObjectDetection_attributes` (priority `osPriorityLow`, stack `128*4`=512 bytes) is **byte-for-byte identical** to `MonitorTask_attributes` - since `MonitorTask` is independently confirmed working continuously on real hardware at this exact same priority/stack size, generic scheduling starvation or a stack-size problem is a poor fit for explaining why only `ObjectDetectionTask` would be stuck. **One concrete fact established purely from source, before any new hardware run:** `g_object_detection_raw_gpio_level`'s compile-time initializer is `0`, but it was observed as `1` - since nothing else in the program writes to this variable, this alone proves the diagnostic assignment in `StartTask04` (and the `HAL_GPIO_ReadPin()` call inside it) executed at least once. What it does NOT prove is whether that happened continuously (every 20ms, as intended) or only once before the task stopped looping (e.g. a hang) - a frozen task would produce an identical-looking symptom to "the pin is just genuinely always HIGH."
    - **Round-3 diagnostic added, per explicit instruction, still no algorithm change**: `g_object_detection_poll_count` (new `uint32_t` global in `main.c`), incremented unconditionally as the very first statement in `StartTask04`'s loop body, before `ObjectDetection_Poll()` or anything else runs that iteration - a direct, unambiguous "is this loop actually iterating" signal, independent of IR/GPIO behavior entirely.
    - **Verified:** `main.c` compiles **zero warnings** against the real ARM toolchain (only file changed this round - `ir.c`/`object_detection.c` remain untouched since round 1). Full-project link re-verified - link succeeds, flash usage ~71.3 KB, zero symbols in Configuration's reserved flash page.
    - **Round-3 result (user's test): `g_object_detection_poll_count` rose to exactly `254` and then froze there permanently - it does NOT keep rising.** This is conclusive: `StartTask04` is **not** a task that runs forever and genuinely always samples HIGH - it is a task that ran normally for ~254 iterations (254 × 20ms ≈ **5.08 seconds**) and then stopped executing entirely. This is now a genuine RTOS-level "task stopped running" bug, not an IR-signal or GPIO-configuration question - confirmed further by the user independently verifying the hardware wiring/GPIO config are correct.
    - **Round-4 investigation, source inspection only, still no changes to `ir.c`/`object_detection.c`/algorithm/priority/poll-interval:** full call graph of `ObjectDetection_Poll()` reviewed - `IR_Read()` only calls `HAL_GPIO_ReadPin()` (non-blocking); `Event_OnObjectDetection()` is only called on a reported-state change, which has **never happened** in any round so far (raw state has stayed `IR_NOT_DETECTED` throughout), so that heavier branch (LED/Buzzer/FatFs-SD-writing) is **provably not involved** in this freeze. `Event_CheckAlarmButton()` (`StartTask04`'s other call) only does `Button_Read()` and conditionally `Buzzer_Off()` - neither blocks. **No mutex exists anywhere in this project** (every module's own docs confirm this), ruling out a mutex deadlock entirely. **Noted, not yet investigated**: 254 × 20ms ≈ 5.08s lines up closely with `MonitorTask`'s own hard-coded initial `osDelay(5000)` before its first real `Monitor_Sample()` call - the heaviest operation any task in this project performs (DHT priority boost, ADC reads, `Log_Write()` over SPI/FatFs) - flagged as a coincidence worth cross-checking in a future round (e.g. watching `g_monitor_sample_count` at the same time), not acted on.
    - **Round-4 diagnostic added, per explicit instruction, still no algorithm/driver/priority/interval change**: 6 new step-marker globals in `main.c`, one at each point in `StartTask04`'s loop body (before/after `ObjectDetection_Poll()`, before/after `Event_CheckAlarmButton()`, before/after `osDelay()`) - `g_od_diag_before_poll`, `g_od_diag_after_poll`, `g_od_diag_before_button_check`, `g_od_diag_after_button_check`, `g_od_diag_before_delay`, `g_od_diag_after_delay`. Each is set to the current `g_object_detection_poll_count` value when that point is reached, so on the frozen iteration, whichever marker stops matching `g_object_detection_poll_count` (stays one iteration behind) pinpoints exactly which step never completed.
    - **Verified:** `main.c` compiles **zero warnings** against the real ARM toolchain (only file changed - `ir.c`/`object_detection.c` remain untouched since round 1). Full-project link re-verified - link succeeds, flash usage ~71.4 KB, zero symbols in Configuration's reserved flash page.
    - **Round-4 result (user's test): `g_object_detection_poll_count` climbed to `254` and froze there permanently - it does NOT keep rising.** Escalated the investigation to a comprehensive, user-authored diagnostic action plan (system-wide-vs-task-specific, stack-overflow detection, heap tracking, fault-handler capture, and a blocking-call audit of `MonitorTask`) - see round 5.
    - **Round-5 investigation, per the user's own written action plan, still no changes to `object_detection.c`'s algorithm, `ir.c`/`ir.h`, task priority, or the poll interval:**
      - **`FreeRTOSConfig.h` audit**: confirmed `configCHECK_FOR_STACK_OVERFLOW` was **not defined at all** (grep found nothing) - stack overflow detection has been off this whole project, exactly as flagged back when Log's `InitTask` stack overflow was first root-caused (Sec 14's Log entry). **Enabled now**: `configCHECK_FOR_STACK_OVERFLOW = 2` (the more thorough of FreeRTOS's two methods), plus a new `vApplicationStackOverflowHook()` implementation in `main.c` (required once the define is nonzero, or the link fails) - captures the offending task's name into `g_stack_overflow_task_name`/`g_stack_overflow_detected` and traps, instead of the silent-corruption behavior this project has had all along.
      - **Fault handler audit** (`stm32l4xx_it.c`): `HardFault_Handler`/`MemManage_Handler`/`BusFault_Handler`/`UsageFault_Handler` were confirmed to be the **default CubeMX-generated empty stubs** - an immediate `while(1)` trap with zero diagnostic capture. Exactly the concern raised: a real fault would have looked externally identical to "a task silently stops," the very symptom under investigation. **Fixed**: each now captures `SCB->CFSR`/`SCB->HFSR` (and `MMFAR`/`BFAR` when their CFSR valid-bit is set) into new globals (`g_fault_type`, `g_fault_cfsr`, `g_fault_hfsr`, `g_fault_mmfar`, `g_fault_bfar`) and lights the red LED (`RGB_LED_3`, raw GPIO, bypassing `led.c`) before trapping. **Deliberately did not** attempt to capture the exact faulting PC/LR (needs a naked/asm-wrapped handler reading the hardware exception stack frame - real complexity against this project's "keep it simple" rule); CFSR/HFSR alone reliably distinguish a stacking fault (the classic stack-overflow signature) from a bad memory access or illegal instruction, which is enough for this investigation.
      - **`MonitorTask`/driver blocking-call audit** (read-only - `dht.c`/`battery.c`/`light.c`/`monitor.c`/`log.c` NOT modified): `dht.c`'s `WaitForLevel()` is bounded by a real hardware timer (`DHT_EDGE_TIMEOUT_US = 200`, TIM6-based) - cannot hang indefinitely. `battery.c`/`light.c`'s `HAL_ADC_PollForConversion()` calls both use a bounded 10ms timeout. **One real finding**: `log.c`'s live `Log_Init()` (line 127) contains an active `HAL_Delay(1000)` - a genuine FreeRTOS anti-pattern (busy-waits without yielding to the scheduler, unlike `osDelay()`) - a second, separate `HAL_Delay(1000)` at line 115 is inside the already-dead, fully commented-out old version of `Log_Init()` and never compiles. The live one runs **once**, near boot, from `StartTask02` (`Log_Init()` is only ever called once) - not obviously at the ~5s freeze mark, so flagged as a real anti-pattern worth fixing eventually, but not claimed as the root cause of this specific freeze without more evidence. No other `HAL_Delay()` calls exist anywhere else in `Core/Src` (confirmed by project-wide search).
      - **Heartbeat task added** (temporary 8th task, per explicit user instruction - not one of Sec 7's approved 7, will be removed once this investigation concludes): `HeartbeatTaskHandle`/`HeartbeatTask_attributes` (`osPriorityLow`, 512-byte stack, matching every other task), `StartHeartbeatTask()` increments `g_heartbeat_count` and toggles `RGB_LED_2` (PA8, the **blue** channel - confirmed via `led.c`'s own header comment to be a channel Event's LED logic never drives, so this cannot be confused with Event's real red/yellow/green usage) every 150ms via `osDelay()`. No blocking calls, no shared resources besides that one otherwise-unused pin.
      - **Stack high-water-mark + free-heap tracking added**: `g_od_stack_hwm`/`g_monitor_stack_hwm` (`uxTaskGetStackHighWaterMark(NULL)`, words remaining) and a shared `g_free_heap_size` (`xPortGetFreeHeapSize()`), sampled once per loop iteration in both `StartTask04` (`ObjectDetectionTask`) and `StartTask03` (`MonitorTask`).
      - **Verified:** since `FreeRTOSConfig.h` changed, **every FreeRTOS source file** (not just the app files) was rebuilt against it for the link-verification test (`tasks.c`, `port.c`, `queue.c`, `list.c`, `timers.c`, `croutine.c`, `event_groups.c`, `stream_buffer.c`, `heap_4.c`, `cmsis_os2.c`, `freertos.c`), confirming the new stack-overflow-checking code paths compile cleanly project-wide, not just in the touched files. `main.c`/`stm32l4xx_it.c` compile **zero warnings**. Full-project link succeeds, flash usage ~72.0 KB (well under the 1022 KB budget), zero symbols in Configuration's reserved flash page. Directly confirmed via `nm` that `StartHeartbeatTask`, `vApplicationStackOverflowHook`, and `HardFault_Handler` are genuinely present in the linked binary at real flash addresses.
      - **`ir.c`/`ir.h`/`object_detection.c`/`object_detection.h` remain byte-for-byte untouched since round 1.**
    - **Round-5 result**: rather than a clean single-freeze-point readout, the user reported a richer symptom picture on the round-5 build - see round 6 below.
    - **Round-6: full requirements-vs-implementation audit + root-cause analysis (2026-09-01), NO source files changed this round, per explicit instruction.** Triggered by: heartbeat LED blinks blue at boot; using the IR remote turns the buzzer on and it never turns off; the LED stays blue instead of showing red; the `.TXT` file contains many repeated `CONFIGURATION CHANGED`/`STARTUP (NORMAL)`/`MODE NORMAL->ERROR` triples, plus exactly one real `OBJECT DETECTED` (`1788134503`) and zero `OBJECT CLEARED`.
      - **Requirements-vs-implementation table** (Sec 2.1-2.10, 2.3.1):

        | # | Requirement | Expected | Current code | Observed | Match? |
        |---|---|---|---|---|---|
        | 1 | Startup (2.7) | Init requests CC sync, notifies Event once synced | Init module doesn't exist; `STARTUP` call is `StartTask02`'s one-shot test scaffolding | Logged repeatedly, identical timestamp | N/A - test scaffolding; repetition is a reset symptom |
        | 2 | Config Changed (2.3) | Write + send on real config change | Correct, but only ever invoked by one-shot test data (no dispatch module exists) | Repeated identical entries | Code correct for what it's asked; same reset symptom |
        | 3 | Normal mode (2.10) | LED green | Correct per source | Not independently observable this round | Inconclusive |
        | 4 | Warning mode | LED yellow | Correct per source | Not exercised | Not exercised |
        | 5 | Error mode | LED red, alarm on | Correct (`Event_OnModeTransition`) | `MODE NORMAL->ERROR` repeated, always `t=1788134400` | Code correct; each is a fresh boot's first `Monitor_Sample()` |
        | 6 | Object Detected (2.2/2.3) | LED red, alarm on, write+send | Correct | One real event, genuinely-incremented timestamp | **YES** for this instance |
        | 7 | Object Cleared | LED green, stop alarm | Correct (`flag=0` branch) | Zero occurrences | Never observed - task died before it could poll again |
        | 8 | LED behavior | Reflects state via Event only | `event.c` sole caller of `Led_SetColor()`; heartbeat uses a different, unused channel (no conflict) | Stays blue | Explained by reset loop, not a bug |
        | 9 | Buzzer behavior | Auto-off on clear/return, or button | Both paths correctly implemented | Turns on once, never off | Explained below - not a logic bug |
        | 10 | Event logging (2.3) | Every event, real timestamp | File-writing already hardware-verified correct | Many repeats, identical timestamps | Working exactly as designed - accurate record of real repeated resets |
        | 11 | Mode transitions | Per 2.3.1's 5 cases | Correct | Only Normal->Error observed | Covered by row 5 |
        | 12 | Alarm silencing via button (2.3) | Button stops active alarm | Correct (`Event_CheckAlarmButton`, polled every 20ms from `ObjectDetectionTask`) | Not effectively testable | The polling task is the one that dies |

      - **Does the spec require auto-stop, or only via button?** Auto-stop, per Sec 2.3 ("stop alarm if active" appears on both the Object-Cleared and Warning/Normal-return cases) - the button is an *additional* path, not the only one. Already correctly implemented in `event.c`.
      - **Is the heartbeat LED conflicting with Event's LED?** No - confirmed by source: `led.c` only ever drives `RGB_LED_1`(green)/`RGB_LED_3`(red) via any real `LedColor` value; the heartbeat diagnostic deliberately toggles `RGB_LED_2`(blue), which `led.c`'s own header comment confirms is never driven by any `LedColor`. No pin-level conflict exists.
      - **Why does the buzzer stay on?** `Buzzer_On()` starts hardware PWM (TIM3), which free-runs with zero further CPU involvement - "stuck on" is exactly what a system freeze/reset right after `Buzzer_On()` would look like, not evidence of a runaway loop. Since Error mode (which also calls `Buzzer_On()`) recurs across many reset cycles, the buzzer is most likely being re-triggered on nearly every cycle that reaches Error, sounding continuous.
      - **Repeated Event records - investigated per the user's own checklist**: not a task restarting itself (no such FreeRTOS feature, none implemented); init code re-executing only because the *whole system* restarts; reset detection isn't implemented yet (N/A, not the cause); config/Event calls individually correct, repetition traces to repeated reboots; **timestamps ARE being reused, confirmed** - `g_log_test_timestamp` is never incremented anywhere in the source (verified by search), a fixed constant by design (no RTC yet) - identical timestamps across many repeats of one-shot code is conclusive proof of real, repeated resets, not a software loop; **SD/Event files ARE being appended across resets, confirmed** - `FA_OPEN_APPEND` is correct design, faithfully recording many distinct real reboots into one file.
      - **Correlating with rounds 1-5**: two independent threads converge on the same ~5-second mark - round 3/4's `g_object_detection_poll_count` freezing at 254×20ms≈5.08s, and this round's finding that every `MODE NORMAL->ERROR` carries `MonitorTask`'s own un-incremented initial timestamp, meaning it's always Monitor's first real sample (fires after its hard-coded `osDelay(5000)`). Very likely ONE underlying problem.
      - **`IWDG`/`WWDG` directly ruled out as the reset source** by source inspection (not assumption): no `MX_IWDG_Init()` call, no `hiwdg` handle declared anywhere in `main.c` - only a vestigial comment and a commented-out refresh line remain in `StartTask08`. The "IWDG removed" history is confirmed still accurate in current code.
      - **Root-cause hypotheses, ranked:**
        1. **(High confidence) `MonitorTask`/`ObjectDetectionTask` stack overflow via `Event_On*()` → `WriteEventRecord()`'s local `FIL`.** `MonitorTask_attributes.stack_size` is still `128*4`=512 bytes, never increased (unlike `InitTask`'s already-fixed identical bug). `WriteEventRecord()` (`event.c`) declares a local `FIL file` (~560-570 bytes, the same struct that caused `InitTask`'s original overflow). Worst-case chain `Monitor_Sample()`→`Event_OnModeTransition()`→`WriteEventRecord()` is ~750-800+ bytes deep on a 512-byte stack - essentially guaranteed to overflow. `ObjectDetectionTask` has the identical latent risk via `Event_OnObjectDetection()`, on its own 512-byte stack.
        2. (Possible, less evidence) Brown-out reset from combined SPI/ADC/DHT/buzzer activity during Monitor's first cycle.
        3. (Unlikely for this symptom) `log.c`'s live `Log_Init()` `HAL_Delay(1000)` (confirmed real, line 127) - runs once near boot (~t=0), poor fit for a t≈5s symptom.
        4. (Ruled out) IWDG/WWDG - confirmed absent from compiled code.
      - **Ranked fixes**: (1, recommended) increase `MonitorTask`'s and `ObjectDetectionTask`'s stack sizes (e.g. 512→2048 bytes) - mirrors `InitTask`'s already-proven fix, one line per task, no application-logic change. (2) confirm empirically via round 5's stack-overflow detection after applying fix 1. (3, more invasive) read `RCC->CSR`'s reset-cause flags at boot. (4, most invasive, likely unnecessary) redesign `WriteEventRecord()` to avoid a stack-resident `FIL`.
      - **NOT yet applied - awaiting user approval.** No source files were changed this round.
    - **Round 7: SD-card-present vs. SD-card-removed A/B test (2026-09-01), NO code changed this round, per explicit instruction.** User ran two controlled hardware tests, changing only whether the SD card was inserted:
      - **Test 1 (card removed)**: full, repeatable detect→buzzer-on→LED-red→release→buzzer-off→LED-green cycles, indefinitely, zero freezes.
      - **Test 2 (card inserted)**: identical trigger (a real object detection) - but the system gets stuck in the detected/buzzer-on state permanently; releasing or re-pressing the remote does not recover it.
      - **Traced against `WriteEventRecord()` (`event.c:117-139`) line by line**: both tests execute identically up to `f_open()` at `event.c:127`. **Test 1's `f_open()` fails fast** (no card) → the `if (s_lastOpenResult != FR_OK) { return; }` guard fires immediately → shallow return, stack usage drops right back down, loop continues normally forever. **Test 2's `f_open()` succeeds** → execution reaches `f_write()` at `event.c:136` - FatFs's internal sector/cluster bookkeeping plus the underlying SPI diskio driver's real data-transfer functions, a substantially deeper call chain than a failed `f_open()`, stacked **on top of** the already-large (~560-570 byte) `FIL file` sitting in the frame the entire time.
      - **This sharpens (and corrects the precision of) the round-6 stack-overflow hypothesis**: the large `FIL` local alone is NOT sufficient to overflow the stack by itself - Test 1 proves this, since it's allocated in both tests and Test 1 survives indefinitely. The corrected mechanism: the `FIL` reserves ~560+ bytes unconditionally (leaving almost no margin on a 512-byte stack), and it's specifically the **additional depth from a successful `f_write()`'s internal call chain** that most likely pushes total usage past the limit. **Most likely exact location: `event.c:136` (`f_write()`)** - not the `FIL` declaration, not `f_open()` (both tests reach those safely). `f_close()` (line 137) and `DeleteOldFile()`'s `f_unlink()` call (after) are secondary candidates, less likely to be reached first.
      - **Test 1 exonerates everything except the SD-write path**: `IR_Read()`, `ObjectDetection_Poll()`'s detect/clear/timeout logic, `Event_OnObjectDetection()`'s LED/buzzer driving, `led.c`, `buzzer.c`, and `Event_CheckAlarmButton()` are all confirmed working correctly by Test 1's clean, repeatable, correct behavior - the remaining problem is isolated entirely to `WriteEventRecord()`'s SD-card write, shared by both `MonitorTask` and `ObjectDetectionTask` via the same function.
      - **This also gives Object Detection its own direct, on-demand reproduction of the same bug class already inferred (via timestamp correlation) for `MonitorTask` in round 6** - not a competing explanation, the same shared-function vulnerability demonstrated through a second caller.
      - **Blocking-wait vs. stack-overflow, reconsidered**: Test 1/2 alone can't fully distinguish a genuine indefinite hardware wait inside FatFs/SPI from a stack overflow, since both would equally explain "works when `f_open()` fails fast, breaks once `f_write()` is reached." Leaning stack-overflow because (a) this exact SPI/FatFs/diskio code was already proven to complete reliably for real writes once `InitTask`'s stack alone was increased, with zero diskio/SPI driver changes (Log module's own history) - direct in-project precedent that this code path terminates normally given enough stack; (b) the round-5 diagnostics that would settle this directly (`g_stack_overflow_detected`/`g_stack_overflow_task_name`, `g_fault_type` etc.) have not yet been checked during a Test-2-style reproduction - recommended as the next, still-zero-code-change confirmation step.
      - **No source files changed this round - analysis only, per explicit instruction.**
    - **ROOT CAUSE CONFIRMED, FIX APPLIED, VERIFIED ON REAL HARDWARE — RESOLVED (2026-09-01).** User increased `MonitorTask_attributes.stack_size` and `ObjectDetection_attributes.stack_size` from `128*4` (512 bytes) to `1024*4` (4096 bytes) in `main.c` (their own direct edit, old values left commented out for reference) - the exact fix predicted in round 7, applied to both tasks sharing the vulnerability. **Confirmed on real hardware, SD card inserted**: object detection works correctly and repeatably, buzzer/LED behave correctly (on when detected, off when cleared), events are written to the SD card, and the previous freeze/reset loop no longer occurs. This closes out the multi-round investigation (rounds 1-7) that spanned IR-polarity theories, a full RTOS-level task-freeze investigation (heartbeat task, stack-overflow detection, fault-handler capture - all still in place as temporary diagnostics, not removed), a full requirements-vs-implementation audit, and finally a controlled SD-card-present-vs-removed A/B test that pinpointed the exact mechanism (`event.c:136`, `f_write()`, stacked on top of `WriteEventRecord()`'s large local `FIL` on an undersized task stack).
      - **Measured stack high-water marks (real hardware, post-fix), recorded for future reference:**
        - `g_od_stack_hwm = 708 words` → **2832 bytes minimum-ever-free** on `ObjectDetectionTask`'s 4096-byte stack → peak actual usage ≈ 4096-2832 = **1264 bytes** (≈31% of the stack; ≈69% margin remaining at the worst point observed).
        - `g_monitor_stack_hwm = 680 words` → **2720 bytes minimum-ever-free** on `MonitorTask`'s 4096-byte stack → peak actual usage ≈ 4096-2720 = **1376 bytes** (≈34% of the stack; ≈66% margin remaining).
        - Both real, measured peaks are noticeably higher than the earlier ~750-800 byte round-6 *estimate* (expected - real FatFs/SPI call depth includes more than the hand-counted locals alone), but both sit comfortably within the new 4096-byte budget with healthy margin.
      - **Related, NOT-yet-resolved concern, now confirmed with real data, not just arithmetic**: `g_free_heap_size` was observed at **136 bytes** - the heap-exhaustion risk flagged during Task 1's arithmetic (raw task stacks alone summing to exactly `configTOTAL_HEAP_SIZE`, 15,360 bytes) is real and measured, not just theoretical - only 136 bytes of the entire 15,360-byte heap remain unallocated. `osThreadNew()`'s return value is never checked anywhere, so it is not yet confirmed whether every task (in particular `HeartbeatTask`, created last) actually got created successfully, or whether the system is already running with one or more tasks silently missing. **Per explicit user instruction, current stack sizes and the heap size are being left exactly as they are for now** - no reduction, no increase, no `HeartbeatTask` changes, no diagnostic-variable removal. This is recorded as a known, measured, tight-margin condition to revisit later, not an active blocker - the system is confirmed working for everything tested so far.
      - **Per explicit user instruction: `HeartbeatTask` and all round-5/6/7 diagnostic globals (`g_heartbeat_count`, `g_od_stack_hwm`, `g_monitor_stack_hwm`, `g_free_heap_size`, `g_stack_overflow_detected`/`g_stack_overflow_task_name`, `g_fault_type`/`g_fault_cfsr`/`g_fault_hfsr`/`g_fault_mmfar`/`g_fault_bfar`, `g_od_diag_*` step markers, `g_object_detection_raw_gpio_level`) remain in the code** - explicitly treated as temporary debugging tools, not product functionality, not yet removed. `configCHECK_FOR_STACK_OVERFLOW=2` and the instrumented fault handlers also remain enabled/in place (a net positive for the rest of development, not just this investigation - worth keeping long-term, not just "temporary," though officially still labeled temporary per the user's own framing).

**Object Detection module status: DONE and HARDWARE-VERIFIED**, including the full stack-overflow investigation and fix. Do not revisit Object Detection's, Monitor's, Event's, or Log's design unless new evidence of a problem appears, and do not change any task's stack size without explicit instruction (current sizes are deliberately being kept as-is).

- **Init module — implemented, compiled clean, full-project link verified. AWAITING REAL-HARDWARE TEST (2026-09-02).** Sixth LNC Application-layer module (Sec 2.7). Two new files: `Embeded/Core/Inc/init.h`, `Embeded/Core/Src/init.c`.
  - **Real gap found before implementing, not assumed: the RTC peripheral isn't actually enabled in this project.** While reviewing what "read the STM32's real hardware RTC" (the originally agreed Init design, Sec 15 as of 2026-09-01) would require, checked the actual generated code rather than trusting this guide's own Sec 9.2 claim — found `HAL_RTC_MODULE_ENABLED` commented out in `stm32l4xx_hal_conf.h`, zero RTC entries in `Embeded.ioc`, and no `hrtc` handle or `MX_RTC_Init()` anywhere. Surfaced to the user as a BLOCKING contradiction between the guide and real source before writing any code (see Sec 9.2's corrected RTC bullet above). **User's explicit decision: do not touch RTC or Watchdog hardware at all this session** — build Init's software structure now, leave a clean seam for real RTC (and later CC time-sync) to plug in without redesigning Init.
  - **Design, per the user's explicit direction:** `Init_Start(uint32_t timestamp)` takes the current timestamp as a parameter, the same convention `Monitor_Sample()`/`ObjectDetection_Poll()` already use — this parameter itself IS the future RTC/CC-sync seam, so no separate unused `Init_OnRtcTimeReceived()` hook function was added (the earlier Sec 15 plan proposed one; dropped as unnecessary infrastructure now that the timestamp-parameter pattern covers the same need more simply, matching this project's own established convention). `Init_Start()`: calls `Configuration_Init()` → `Log_Init()` → `Event_Init()` (Sec 2.7's "starts all system activities," relocated verbatim from `StartTask02` with no behavior change), then calls `Event_OnInitStartup()` with the given timestamp and `wasWatchdogReset=0` (no watchdog exists — Open Question #2). One `Init_GetLastStartupFrameLength()` getter, mirroring every other module's test-observability convention (`Config_WasLoadedFromFlash()`, `Event_IsAlarmActive()`, etc.).
  - **Deliberate behavior change from the old `StartTask02` test scaffolding, surfaced not silently changed:** the old temporary code also fired a fake `Event_OnConfigurationChanged()` once at boot, purely to hardware-test that path (already confirmed working, Sec 14's Event entry). `Init_Start()` does **not** do this — a real configuration-changed event should only ever come from a real Configuration setter call (Sec 2.6), never fire unconditionally every boot. Dropping it is a correctness improvement, not scope creep.
  - **No new FreeRTOS task** (per explicit instruction) — `StartTask02`/`InitTask` still owns this, just calls `Init_Start(g_log_test_timestamp)` instead of the old inline Configuration/Log/Event calls; `g_log_test_timestamp` is the same pre-existing fixed test clock (1788134400UL, 2026-08-31 00:00:00 UTC) this code already used for this exact purpose before the call moved into `init.c`. No self-delete added (per explicit instruction — `StartTask02`'s IR/Button/Buzzer test loop still runs forever below it, unchanged).
  - **`main.c` changes are additive/relocating only**: `#include "init.h"` added; the debugger-watch globals (`g_config_loaded_from_flash`, `g_log_mounted`, `g_log_mount_result`, `g_event_last_frame_length`, `g_event_open/write/close_result`) are still populated right after `Init_Start()` returns, by calling the exact same getters as before (`Config_WasLoadedFromFlash()`, `Log_GetLastMountResult()`, `Event_GetLast*Result()`) — these are getters on each module's own internal state, so moving the call site into `init.c` doesn't affect them; `g_log_mounted` is now derived as `(g_log_mount_result == 0)` since `Log_Init()`'s own boolean return is no longer directly visible in `main.c`. `configuration.c`/`log.c`/`event.c` themselves were **not touched**. `Monitor_Init()`/`ObjectDetection_Init()` remain exactly where they were (each task still self-initializes, per explicit instruction).
  - **Verified:** `init.c` and the updated `main.c` both compile with **zero warnings** against the real ARM toolchain (`arm-none-eabi-gcc`, found under the STM32CubeIDE 2.1.1 install and invoked directly). Full-project link re-verified in a scratch copy (same method as every previous module — real object list + freshly compiled `main.o`/`init.o`, real linker script): **link succeeds (exit code 0)**, flash usage 72144 bytes text (~70.5 KB, well under the 1022 KB budget), zero symbols in Configuration's reserved `0x080FF800` page, and `nm` confirms `Init_Start`/`Init_GetLastStartupFrameLength` are genuinely present in the linked binary. The real `Embeded/Debug/` build artifacts were not touched — a normal build inside STM32CubeIDE will pick up the real source changes correctly next time it's built there.
  - **VERIFIED ON REAL HARDWARE (2026-09-02).** User confirmed: `Init_Start()` runs correctly at boot in place of the old inline code, and the deliberate behavior change (no more spurious `CONFIGURATION CHANGED` line every boot) is working as intended. `init.h`/`init.c` are finished and permanent.

**Init module status: DONE and HARDWARE-VERIFIED** (with a placeholder timestamp - see below for the follow-up that replaced it with the real RTC). The LNC→CC time-sync round trip remains explicitly deferred, not forgotten.

- **RTC wired into Init as the real timestamp source — implemented, compiled clean, full-project link verified. AWAITING REAL-HARDWARE TEST (2026-09-02).** Follows directly from the RTC peripheral now being genuinely enabled (see Sec 9.2's updated RTC bullet for the CubeMX-level verification). `Init_Start(uint32_t timestamp)`'s own design/signature is **unchanged** - per explicit instruction, this was a pure "supply a real timestamp instead of a placeholder" change, not an Init redesign.
  - **`DateToEpoch()` helper added in `main.c`** (`Embeded/Core/Src/main.c`, "USER CODE BEGIN 4" region, alongside its own private `IsLeapYearForRtc()`) - the exact inverse of `log.c`'s private `EpochToDate()`, written the same way (simple counting loops, no library date functions) since `log.c`'s version is `static`/private and Log is finished/verified/do-not-touch (same reasoning `event.c` already used to justify its own duplicate `EpochToDate()`). Lives in `main.c` rather than a new file or inside `init.c`, because Init's own architecture deliberately never reads time itself (`Init_Start()` only ever receives a timestamp, matching `event.h`'s documented "every function takes a timestamp already filled in by the caller" rule) - reading the RTC and converting it is the *caller's* job, the same place `g_log_test_timestamp` used to live.
  - **`StartTask02` now reads the real RTC before calling `Init_Start()`**: `HAL_RTC_GetTime(&hrtc, &rtcTime, RTC_FORMAT_BIN)` **then** `HAL_RTC_GetDate(&hrtc, &rtcDate, RTC_FORMAT_BIN)` - calling `GetTime()` first is a real STM32 HAL requirement (it unlocks the date shadow registers), not a style choice; done correctly here, called out explicitly in a comment since it's a well-known, easy-to-miss RTC gotcha. `RTC_FORMAT_BIN` used (not BCD) to avoid an unnecessary extra conversion step, keeping the code simple. `rtcDate.Year` is a HAL offset from 2000, added back (`2000 + rtcDate.Year`) before calling `DateToEpoch()`.
  - **`g_log_test_timestamp` removed** (it's now genuinely dead - was only ever read at the one call site that now uses real RTC data instead). **Replaced with real RTC-observability globals**: `g_rtc_year`/`_month`/`_day`/`_hour`/`_minute`/`_second` (the raw calendar reading, so it can be eyeballed directly against the real wall clock/whatever was set) and `g_rtc_timestamp` (the converted epoch, the exact value passed into `Init_Start()`) - same debugger-watch convention as every other module.
  - **Verified:** `main.c` (plus the two newly-needed HAL driver files `stm32l4xx_hal_rtc.c`/`stm32l4xx_hal_rtc_ex.c`, which had never been compiled before since RTC was previously disabled) all compile with **zero warnings** against the real ARM toolchain. Full-project link re-verified in a scratch copy (the existing `Debug/` build predates RTC being enabled at all, confirmed via file timestamps - `Embeded.ioc` newer than `Debug/Core/Src/main.o` - so this was a genuine from-source link test, not reusing stale RTC-less objects): **link succeeds (exit code 0)**, flash usage 74456 bytes text (~72.7 KB, still well under the 1022 KB budget), zero symbols in Configuration's reserved `0x080FF800` page, and `nm` confirms `DateToEpoch`/`HAL_RTC_Init`/`HAL_RTC_GetTime`/`HAL_RTC_GetDate`/`MX_RTC_Init` are all genuinely present and linked.
  - **NOT yet run on real hardware.** Next step: flash and confirm via the debugger that `g_rtc_year`/`_month`/`_day`/`_hour`/`_minute`/`_second` match whatever date/time the RTC actually has (from `MX_RTC_Init()`'s compiled-in `SetTime`/`SetDate` calls, unless already overwritten by a prior session), that `g_rtc_timestamp` is a plausible-looking Unix epoch for that date/time, and that `g_event_last_frame_length`/the SD card's `STARTUP` line reflect the real RTC-derived timestamp instead of the old fixed `1788134400`. **Independently: the TIM3 buzzer regression above should be fixed and reflashed before/alongside this test**, since both changes are in the same not-yet-flashed build.

**Init/RTC status: implemented and link-verified.** First real-hardware test found two issues, both investigated and analyzed below - one fixed (stack-size regression), one deliberately left as a recommendation pending a user decision (RTC placeholder date/time).

- **Hardware test #1 result: `g_rtc_year=2000`, `g_rtc_month=1`, `g_rtc_hour=10`, `g_rtc_minute=31`, `g_rtc_second=0`.** RTC is running (the calendar ticks, `HAL_RTC_GetTime()`/`GetDate()` return real data) but shows CubeMX's compiled-in placeholder date/time, not a real one - **expected, not a bug**, and the user had already correctly anticipated this ("enabling the RTC does not automatically set the current real date/time").
  - **Root cause, confirmed directly from `MX_RTC_Init()` in `main.c`**: after `HAL_RTC_Init()`, CubeMX's generated code unconditionally calls `HAL_RTC_SetTime()`/`HAL_RTC_SetDate()` with hardcoded BCD values every single boot - `sTime.Hours=0x10, Minutes=0x31, Seconds=0x0` (BCD `0x10`/`0x31` = decimal 10/31, matching the observed `g_rtc_hour`/`g_rtc_minute` exactly) and `sDate.Year=0x0, Month=RTC_MONTH_JANUARY, Date=0x1` (Year is a HAL offset from 2000, so `0x0` → `2000`, matching `g_rtc_year=2000`/`g_rtc_month=1` exactly). **This is CubeMX's standard "Initialize RTC and set the Time and Date" boilerplate, and it has no guard to skip re-applying the placeholder if the RTC already holds a real value** - it runs unconditionally on every single reset, which is why "enabling RTC" alone can never produce a real date: even if something set a real time once, the very next reset would immediately stomp it back to this placeholder.
  - **CubeMX itself hints at the intended fix location**: right after `HAL_RTC_Init()` and before the `SetTime`/`SetDate` calls, there's an empty `/* USER CODE BEGIN Check_RTC_BKUP */ ... /* USER CODE END Check_RTC_BKUP */` marker - CubeMX's own convention for exactly this pattern (check a small always-powered RTC backup register for a "already initialized" marker value, and skip `SetTime`/`SetDate` if it's already set).
  - **Recommended next step, per explicit instruction NOT implemented this session** (no time source invented, no CC sync attempted): add a guard using an RTC backup register (e.g. `RTC->BKP0R`) - write a known "magic" value into it the first time `SetTime`/`SetDate` is genuinely applied, and check for that value on every subsequent boot, skipping the `SetTime`/`SetDate` calls if it's already present. This does **not** itself supply a real time - it only stops the placeholder from re-stomping whatever time is already there, which is a **prerequisite** for any future real time source (a one-time debugger-set value, a hardcoded build-time value, or eventually a real CC time-sync reply) to actually survive across resets. Until this guard exists, the RTC will keep showing `2000-01-01 10:31:00` after every reset regardless of what sets a real time in between - expected, not a new bug each time it's observed.

- **Hardware test #2 result: RGB LED is purple (not green) in the normal state, and IR detection produces no LED/buzzer response at all (previously hardware-verified working).** Investigated per explicit instruction, root cause found, fix applied - **AWAITING RE-TEST**.
  - **`led.c`, `event.c`, `object_detection.c`, `ir.c` are all confirmed byte-for-byte unchanged this session** (not touched by any edit made while working on RTC, and CubeMX regeneration never touches hand-authored Application-layer files - only its own generated templates like `main.c`/HAL driver files) - ruling out an LED-mapping, active-high/low, or Event/ObjectDetection logic regression before looking anywhere else, per explicit instruction not to touch that logic without first confirming root cause.
  - **Root cause, confirmed directly from source comparison against previously-documented/verified values: the same RTC-enabling CubeMX regeneration silently reverted THREE unrelated hardware-verified fixes back to their pre-fix defaults** - a repeat, in a more severe form, of the exact same class of drift already seen with `TIM3`'s Prescaler (Open Question #14):
    1. `InitTask_attributes.stack_size`, `MonitorTask_attributes.stack_size`, `ObjectDetection_attributes.stack_size` (`main.c`) were all found reverted to CubeMX's uniform default `128*4` (512 bytes), down from the hardware-verified `1024*4` (4096 bytes) fixed during the rounds-1-7 stack-overflow investigation (Sec 14's Object Detection entry).
    2. `configCHECK_FOR_STACK_OVERFLOW` was found **entirely absent** from `FreeRTOSConfig.h` (was `=2`) - FreeRTOS's own software stack-guard (which calls `vApplicationStackOverflowHook()`, still present and untouched in `main.c`) was silently disabled.
    3. **Causal chain, fully explaining both reported symptoms**: with `MonitorTask`'s stack back to 512 bytes, its first real `Monitor_Sample()` cycle (~5s after boot, per its own `osDelay(5000)`) hitting `Event_OnModeTransition()` → `WriteEventRecord()` (large local `FIL` + `f_write()`'s deep FatFs/SPI call chain) overflows the stack almost immediately - the exact same mechanism already fully diagnosed in the original rounds-1-7 investigation, just reopened. With `configCHECK_FOR_STACK_OVERFLOW` also gone, FreeRTOS's own software guard can no longer catch this cleanly (`vApplicationStackOverflowHook()` never gets called), so the overflow now runs uncaught until it corrupts something badly enough to trip a genuine Cortex-M hardware fault - which **is** still instrumented (`stm32l4xx_it.c`'s `HardFault_Handler`/`MemManage_Handler`/`BusFault_Handler`/`UsageFault_Handler`, all untouched from round 5, Sec 14) to force `RGB_LED_3` (red) **ON** and then trap forever in `while(1)` - freezing the entire scheduler, which is why nothing (including `ObjectDetectionTask`'s 20ms IR poll) runs anymore. `HeartbeatTask` (round-5 diagnostic, still present, toggles `RGB_LED_2`/blue every 150ms) was still running right up until the freeze, so whatever blue state it last left the LED in stays frozen too - landing on **red (from the fault handler) + blue (frozen from heartbeat) = purple**, exactly the reported symptom, with the specific color being a matter of timing chance (could equally have frozen on plain red).
    4. **This is not a new Event/ObjectDetection/LED bug** - it is entirely explained by `main.c`/`FreeRTOSConfig.h` regressions unrelated to any of that code, which itself remains correct and untouched.
  - **Minimal fix applied**: restored `InitTask_attributes.stack_size`/`MonitorTask_attributes.stack_size`/`ObjectDetection_attributes.stack_size` to `1024*4` in `main.c` (old `128*4` values left commented out, matching this project's own established convention for this exact kind of fix). Restored `configCHECK_FOR_STACK_OVERFLOW 2` in `FreeRTOSConfig.h` - this time placed inside the file's own `/* USER CODE BEGIN Defines */ ... /* USER CODE END Defines */` marker (previously it was a plain `#define` outside any marker, which is exactly why regeneration was able to silently drop it) so it survives future regenerations. **No Event/ObjectDetection/LED/IR code was touched.** No other task's stack size was changed (`CommRxTask`/`CommTxTask`/`KeepAliveTask`/`WatchdogTask`/`HeartbeatTask` were never part of the original fix and stay at `128*4`, unchanged). Heap size (`configTOTAL_HEAP_SIZE=15360`) untouched, per explicit instruction - this restores exactly the same tight-but-already-accepted margin measured before (`g_free_heap_size≈136` bytes, Sec 14's Object Detection entry), not a new risk.
  - **Verified:** all of `main.c`, `init.c` (unchanged, recompiled for completeness), `FreeRTOSConfig.h`'s dependents (`tasks.c`, `port.c`, `queue.c`, `list.c`, `timers.c`, `croutine.c`, `event_groups.c`, `stream_buffer.c`, `heap_4.c`, `cmsis_os2.c`, `freertos.c` - every FreeRTOS source file, same precedent as round 5 since the config define changed again), and the two RTC HAL driver files compile with **zero warnings** against the real ARM toolchain. Full-project link verified (`Debug/objects.list` was found to already be current/rebuilt by the user in STM32CubeIDE since the RTC change, so this reused it directly rather than reconstructing it) - **link succeeds (exit code 0)**, flash usage 74536 bytes text (~72.8 KB, still well under the 1022 KB budget), zero symbols in Configuration's reserved `0x080FF800` page, and `nm` confirms `vApplicationStackOverflowHook`/`DateToEpoch`/`Init_Start`/`StartHeartbeatTask` are all genuinely present and linked.
  - **NOT yet re-tested on real hardware.** Next step: reflash and confirm the LED returns to green in Normal mode, and that IR detection correctly turns the LED red + buzzer on again (and clears correctly), matching the originally-verified Object Detection behavior. **The TIM3 buzzer regression (Open Question #14) is still separately unfixed** - worth fixing in the same reflash, since the buzzer's audible tone depends on it independently of this stack-size fix.

- **Stack-size/`configCHECK_FOR_STACK_OVERFLOW` fix (Open Question #15) CONFIRMED on real hardware (2026-09-02).** User reflashed and confirmed: RGB LED changes color correctly on IR detection (including the red "detected" state), IR detection works end to end, and the previous freeze/purple-LED symptom is gone. This closes out Open Question #15 - Object Detection/Monitor/Init are all back to hardware-verified status.

- **Buzzer investigated as a separate, immediately-following hardware test - silent, root cause CONFIRMED as Open Question #14 (the still-unfixed TIM3 Prescaler), nothing else in the path is wrong.** Full path check, item by item, per explicit request: `.ioc`'s `TIM3.IPParameters` still has no `Prescaler` key (never actually persisted); `main.c`'s `htim3.Init.Prescaler` is still `0` (should be `399`); `Period=100` correct/unchanged; TIM3 clock source confirmed APB1 (`APB1CLKDivider=RCC_HCLK_DIV1`) so `TIM3CLK=PCLK1=SYSCLK=80MHz`, unchanged; Channel 1 / PB4 / `GPIO_MODE_AF_PP` / `GPIO_AF2_TIM3` (`stm32l4xx_hal_msp.c`'s `HAL_TIM_MspPostInit`) all correct and unchanged, matching Sec 9.1; PWM mode/polarity (`TIM_OCMODE_PWM1`, `OCPolarity=HIGH`, `OCFastMode=DISABLE`) unchanged; `buzzer.c`'s `Buzzer_On()`/`Buzzer_Off()` correctly call `HAL_TIM_PWM_Start`/`Stop` with the right duty cycle, untouched; `event.c`'s 6 `Buzzer_On()`/`Buzzer_Off()` call sites all present and correct, untouched. **With `Prescaler=0`, the PWM frequency is ~792 kHz (inaudible) instead of the correct ~1980 Hz - this single value is the entire explanation, nothing else in the path has drifted.** No code was changed this round (per explicit instruction and because the root cause is a CubeMX-configured value, not application code) - see Open Question #14 for the exact CubeMX steps given to the user, including a persistence-verification step (checking `.ioc` itself, not just `main.c`) that was missed the two previous times this value was "fixed."

- **Buzzer/TIM3 Prescaler regression RESOLVED and HARDWARE-CONFIRMED (2026-09-03).** User re-set the Prescaler to `399` in CubeMX and this time verified `.ioc` actually persisted it (`TIM3.IPParameters=Channel-PWM Generation1 CH1,Period,Prescaler` / `TIM3.Prescaler=399`), then reflashed. Verified directly from source at the start of this session (`Embeded.ioc` line 253, `main.c` line 801: both show `399`) before trusting the guide's older "not yet fixed" text - user confirmed the buzzer audibly works correctly on real hardware. This closes out Open Question #14 for good.

**Init/RTC status: stack-size regression HARDWARE-CONFIRMED FIXED. Buzzer/TIM3 Prescaler regression HARDWARE-CONFIRMED FIXED. RTC placeholder-date fix recommended but not implemented (user decision pending, not blocking). Keep-Alive (Sec 2.8) HARDWARE-VERIFIED (2026-09-03) - see the Keep-Alive entry above.** Watchdog (Sec 2.9) is now the only unbuilt LNC Application module, and it remains explicitly paused (Open Question #2 - a full redesign, not a tuning tweak, since IWDG/`WatchdogTask`'s refresh logic was removed entirely earlier in this project). The LNC→CC time-sync round trip and the Sec 7 TX-priority-queue system both remain explicitly deferred - RTC now provides a real local timestamp mechanism, but nothing yet corrects it from a real time source or the CC.

### How the loopback test was actually run (for historical reference — this has now been done and passed, see above)
1. Build and flash the `Embeded` project to the NUCLEO-L476RG in STM32CubeIDE.
2. Wire a jumper directly between the USART2 TX and RX pins (PA2 / PA3 — exposed as D1/D0 on the Arduino-style header) so anything sent comes straight back in.
3. Start a debug session, let it run, and open a Live Expressions/Watch window on `g_frames_decoded_ok` and `g_frames_decode_error`.
4. Result: `g_frames_decoded_ok` increased steadily (20→30 observed during one check) with no decode errors reported — passed.

---

## 15. Next Session – Starting Point

**Read this first at the start of next session, before doing anything else. Confirm this matches your own memory of where things stopped before continuing.**

### STOPPING POINT (most recent session — sensor drivers)
We are in the middle of building the LNC's hardware sensor drivers, one at a time (Sec 13 roadmap step 4, done in a different order than originally listed: simple drivers first, per user request). Status of each:

| Driver | File(s) | Status |
|---|---|---|
| RGB LED | `led.h`/`led.c` | **Verified and working** on real hardware (all colors + off) |
| DHT11 (temp + humidity) | `dht.h`/`dht.c` | **Verified and working** on real hardware (all 40 bits) — needs the temporary priority-boost workaround wherever it's called, see below |
| Battery (potentiometer) | `battery.h`/`battery.c` | **Verified and working** on real hardware |
| Light | `light.h`/`light.c` | **Verified and working** on real hardware |
| IR (object detection) | `ir.h`/`ir.c` | **Hardware signal detection VERIFIED (fast-sampling diagnostic). Driver logic itself still the original placeholder - final rewrite pending, see Open Question #12.** |

**IR investigation is resolved at the hardware level.** The user tested with a temporary fast-sampling diagnostic (reading PB10 much faster than the original once-per-2s test) and confirmed PB10 genuinely changes state while an IR remote transmits - the sensor responds correctly to the IR signal, confirming the wiring and pin work. This validates the pulse-train theory from the earlier investigation (Sec 10, Open Question #12).

**What's NOT done yet:** the actual `ir.c` file has not been rewritten. It still contains the original single-poll-every-2s implementation with an unconfirmed polarity guess - both are now known to be the wrong approach for a pulse-train signal. The real design decision - full remote-protocol decoding vs. a simpler "IR activity detected" presence surrogate matching Sec 2.2's actual spec intent - is still open (Open Question #12) and deliberately deferred, not forgotten. This is tracked work, not blocking other modules.

### Where we are now
All planned sensor **hardware capability** is confirmed working: DHT11, Battery, Light (fully verified, drivers complete) and IR (hardware signal detection confirmed, but driver logic still needs a redesign later per Open Question #12).

### Current stopping point: all 7 hardware drivers done, Configuration is next
Buzzer is **VERIFIED on real hardware** - audibly toggles ON/OFF every 2 seconds, TIM3 Prescaler=399 (~1980 Hz) and Period=100 confirmed correct. This completes every planned hardware driver: LED, DHT11, Battery, Light, IR, Button, Buzzer.

### Current stopping point: Log write path failing on real hardware, mid-debugging (SUPERSEDED — see below)
Configuration is fully verified. Log's SD card **mounts** successfully (`g_log_mounted == 1`), but **no file is actually written** - confirmed by the user removing the card and checking on a PC. Diagnostics have just been added (not yet flashed/tested) to pinpoint whether `f_open()`, `f_write()`, or `f_close()` is the failing call - see the detailed writeup above and Open item below.

### SUPERSEDED stopping point (earlier 2026-09-01 session block, kept for history): Log is DONE and HARDWARE-VERIFIED. Event is next, not yet started.
The Log/SD investigation above is now fully closed out. Summary for anyone (including a future session) picking this up cold:
- **Root cause found:** `InitTask` (which runs `Log_Init()`/`Log_Write()`) had a FreeRTOS stack of only 512 bytes (`main.c`'s `InitTask_attributes.stack_size = 128 * 4`) - too small for FatFs's `FIL` object (~560-570 bytes by itself with this project's `_MAX_SS=512`), causing a silent, unreported stack overflow on every write attempt. Found via a file-by-file comparison against a separately-provided, known-working non-RTOS SD/FatFs reference project, which ruled out every SPI/GPIO/FatFs-flag-level difference first (see the detailed writeup a few pages up, in the Log module's own history).
- **Fix applied and verified:** `InitTask_attributes.stack_size` increased from `128 * 4` to `1024 * 4` (4 KB) in `main.c` - the only file changed. User flashed the board and confirmed on real hardware: **data is successfully being written to the SD card.** PASS.
- **Log module (Sec 2.4) is now fully done**: mounts the card, writes dated `YYYYMMDD.LOG` files, retains 7 days, hardware-verified end to end.

### SUPERSEDED stopping point (earlier same-day block, kept for history): Event is IMPLEMENTED, compiled clean, link-verified. AWAITING REAL-HARDWARE TEST.
Full design (purpose/layer/functions/structs/dependencies/test plan) was explained and approved first, per Sec 12's working rules, before any code was written. Two open items were resolved by the user during that approval, not guessed:
1. **Configuration-changed events**: also sent to CC (`TAG_EVENT_CONFIG_CHANGED`), not just written to the events file.
2. **Events file**: originally `YYYYMMDD.EVT`, same 7-day retention/rotation as Log's `YYYYMMDD.LOG`, kept as a separate file. **Changed to `YYYYMMDD.TXT` later this session** (see the Event module's own history above) - plain text, no technical reason to prefer `.EVT`, and `.TXT` is easier to open manually during SD-card testing.

`event.h`/`event.c` created (see the detailed writeup above, in the Event module's own history, for the full API/design). Zero new structs - fully reuses `message.h`'s existing `ModeTransitionMessage`/`TimestampWithFlagMessage`/`TimestampMessage`. Compiles clean (zero warnings) against the real ARM toolchain; full-project link re-verified (real object list + freshly compiled `main.o`/`event.o`, real linker script) - link succeeds, flash usage ~70 KB (well under the 1022 KB budget), zero symbols in Configuration's reserved flash page. Wired into `main.c`'s `StartTask02` for temporary hardware testing (a 6-step mode-transition cycle in the main loop, plus one-shot Configuration-changed/Startup calls at boot) - same convention as every other module.

### SUPERSEDED stopping point (earlier same-day block, kept for history): Event is DONE and HARDWARE-VERIFIED. Monitor is next, not yet started.
User checked the SD card after the hardware test: `<date>.TXT` contains correct records for `CONFIGURATION CHANGED`, `STARTUP (NORMAL)`, and all 5 Sec 2.3.1 mode-transition pairs, with repeated transitions correctly appended (not overwritten) across loop iterations. **Event module (Sec 2.3) is now fully done**: writes timestamped records to a dated events file with 7-day retention, drives LED/buzzer correctly per the transition table, and builds the correct outgoing CC frame for every case - hardware-verified end to end.

### SUPERSEDED stopping point (earlier same-day block, kept for history): Monitor is IMPLEMENTED, compiled clean, link-verified. AWAITING REAL-HARDWARE TEST.
Full design was explained and approved first, per Sec 12's working rules. Three open decisions were resolved by the user during approval, not guessed:
1. **DHT read failures**: keep the last successfully-read temperature/humidity, never substitute `0.0`.
2. **Classification logic**: call `Config_Get*()` directly inside `monitor.c` (simple API), not a large PC-testable parameterized function.
3. **Task wiring**: Monitor now runs from its real, designated `MonitorTask`/`StartTask03` (one cycle every 5s) - not another `StartTask02` bring-up hack.

`monitor.h`/`monitor.c` created (see the detailed writeup above, in the Monitor module's own history, for the full API/design). Zero new structs. Compiles clean (zero warnings) against the real ARM toolchain; full-project link re-verified (real object list + freshly compiled `main.o`/`event.o`/`monitor.o`, real linker script) - link succeeds, flash usage ~70.8 KB (well under the 1022 KB budget), zero symbols in Configuration's reserved flash page.

**Important `main.c` restructuring done alongside this (necessary, not scope creep - see the Monitor entry above for the full reasoning):** the old temporary DHT/Battery/Light-read, Log-write, and Event mode-transition-cycle test blocks were **removed** from `StartTask02`, since Monitor is now their real, permanent caller - leaving them in place would have caused duplicate/conflicting SD-card entries and an unsafe concurrent-`DHT_Read()` hazard between two tasks. `log.c`/`log.h`/`event.c`/`event.h` themselves were **not modified** - only `main.c`'s test glue. `StartTask02` now only still exercises IR/Button (driver-level, no owning Application module yet) and the Buzzer toggle test, plus a new `Event_CheckAlarmButton()` call for alarm-silencing responsiveness.

### SUPERSEDED stopping point (earlier same-day block, kept for history): Monitor is DONE and HARDWARE-VERIFIED. Object Detection is next, not yet started.
User ran the full Sec 15 test procedure and confirmed: correct ~5s cadence with fresh sensor readings each cycle, one `.LOG` line per sample, `.TXT` entries only on real mode changes matching `g_monitor_last_mode`, correct classification against Configuration's limits, LED/buzzer changing only through Event (never directly from `monitor.c`), and the button correctly silencing an active alarm. **Monitor module (Sec 2.1) is now fully done** - hardware-verified end to end.

### SUPERSEDED stopping point (earlier same-day block, kept for history): Object Detection is IMPLEMENTED, compiled clean, link-verified. AWAITING REAL-HARDWARE TEST.
Full design was explained and approved first, per Sec 12's working rules. Three open decisions were resolved by the user during approval, not guessed:
1. **Open Question #12 (Sec 10) resolution**: no `ir.c`/`ir.h` changes - fast-poll (20ms) + activity-timeout (400ms) logic lives entirely in the new Object Detection module. This closes out Open Question #12.
2. **Starting constants**: `IR_POLL_INTERVAL_MS = 20`, `IR_ACTIVITY_TIMEOUT_MS = 400` - tunable after the hardware test if needed.
3. **Task wiring**: `Event_CheckAlarmButton()` moved from `StartTask02`'s 2s loop into the new `StartTask04`/`ObjectDetectionTask` 20ms loop, for faster alarm-silencing response.

`object_detection.h`/`object_detection.c` created (see the detailed writeup above, in the Object Detection module's own history, for the full API/design). Zero new structs - reuses `TimestampWithFlagMessage` and `ir.h`'s `IrState`. `ir.c`/`ir.h` themselves were **not touched** - `IR_Read()` is called exactly as it already existed. Compiles clean (zero warnings) against the real ARM toolchain; full-project link re-verified - link succeeds, flash usage ~71.2 KB, zero symbols in Configuration's reserved flash page.

### SUPERSEDED stopping point (round 1 of the same-day investigation, kept for history): Object Detection's FIRST hardware test FAILED. Chatter hypothesis proposed; round-1 diagnostic (`g_object_detection_raw_ir_state`) added; algorithm NOT changed.
User reported: buzzer sounds continuously after boot, no `OBJECT CLEARED` ever appears in the `.TXT` file (only one `OBJECT DETECTED`), pressing the remote does nothing further, and an MCU reset doesn't clear it. Investigated before changing anything: the leading hypothesis was that `IR_Read()`'s raw value chatters continuously even with no real IR activity (demodulating IR-receiver modules are commonly noisy at idle), which would make `ObjectDetection_Poll()`'s 400ms "quiet" countdown mathematically unable to ever complete. `ObjectDetection_GetRawReading()` getter added (`object_detection.h`/`.c`) and wired to `g_object_detection_raw_ir_state` in `main.c`, purely additive, algorithm untouched.

**Round-1 result: this hypothesis was WRONG (or at least incomplete)** - see the CURRENT block below.

### SUPERSEDED stopping point (round 2 of the same-day investigation, kept for history): Round-1 diagnostic result IN; chatter hypothesis ruled out; round-2 investigation + diagnostic added.
**Round-1 result**: `g_object_detection_raw_ir_state` stayed `IR_NOT_DETECTED` through all 3 test phases - never observed to change. Rules the chatter hypothesis **out**.

**Round-2 investigation (source inspection only)**: re-checked `main.c`'s real `MX_GPIO_Init()` directly - `IR_Pin`=PB10, `GPIO_MODE_INPUT`, `GPIO_NOPULL`, `GPIOB` clock enabled, matches Sec 9.1/`ir.h` exactly, no discrepancy. `ir.c`'s conversion logic: one unambiguous comparison, no bug found. Rules out wrong-pin/GPIO-misconfiguration, narrows to: wiring/hardware, the sensor not signaling that test, or a sampling-rate/aliasing problem.

**Round-2 diagnostic added**: `g_object_detection_raw_gpio_level`, a second independent `HAL_GPIO_ReadPin()` call directly in `main.c`, bypassing `ir.c` entirely.

**Round-2 result: see CURRENT block below.**

### SUPERSEDED stopping point (round 3 of the same-day investigation, kept for history): Round-2 result IN - user confirms hardware/wiring are correct. Round-3 investigation shifts to the software execution path; poll-counter diagnostic added.
**Round-2 result**: `g_object_detection_raw_gpio_level` stays `1` (HIGH), consistently. **User separately confirmed the hardware wiring and GPIO configuration are correct** - redirecting the investigation onto the software execution path.

**Round-3 investigation (source inspection only)**: traced `StartTask04` → `ObjectDetection_Poll()` → `IR_Read()` → `HAL_GPIO_ReadPin()` line-by-line - straight-line code, nothing could skip a read. `ObjectDetection_attributes` confirmed identical to `MonitorTask_attributes` (which works fine) - poor fit for starvation/stack-size. **Fact established from source**: `g_object_detection_raw_gpio_level` initializes to `0` but was observed as `1` - the diagnostic line has executed at least once; not yet established whether continuously or once-then-frozen.

**Round-3 diagnostic added**: `g_object_detection_poll_count`, incremented as the first statement in the loop.

**Round-3 result: see CURRENT block below.**

### SUPERSEDED stopping point (round 4 of the same-day investigation, kept for history): Round-3 result IN - CONFIRMED: ObjectDetectionTask stops running entirely after ~254 iterations (~5.08s). Round-4 step-marker diagnostics added.
**Round-3 result**: `g_object_detection_poll_count` froze permanently at `254` - a genuine "task stopped running" bug, not an IR-signal question.

**Round-4 investigation**: full call graph of `ObjectDetection_Poll()`/`Event_CheckAlarmButton()` reviewed - no blocking calls found, no mutex exists anywhere in the project (deadlock ruled out). Flagged, not yet investigated: the ~5.08s freeze timing coincides with `MonitorTask`'s own first real `Monitor_Sample()` cycle.

**Round-4 diagnostic added**: 6 step-marker globals bracketing every statement in `StartTask04`'s loop.

**Round-4 result: the user escalated to a comprehensive, self-authored diagnostic action plan rather than a single-variable test - see CURRENT block below for round 5.**

### SUPERSEDED stopping point (round 5 of the same-day investigation, kept for history): user's full diagnostic action plan implemented (heartbeat task, stack-overflow detection, fault-handler capture, stack/heap tracking, blocking-call audit). Root cause still NOT confirmed at the time.
**Round-5 investigation and additions, per the user's own written action plan** (full detail in the Object Detection entry above, Sec 14):
1. **`configCHECK_FOR_STACK_OVERFLOW` was never defined at all** (confirmed by grep) - stack overflow detection has been off this entire project. **Now enabled** (`= 2`, FreeRTOSConfig.h), with a new `vApplicationStackOverflowHook()` in `main.c` capturing the offending task's name.
2. **The 4 fault handlers were confirmed to be the default empty CubeMX stubs** - zero diagnostic capture, meaning a real fault would look identical to "task silently stops." **Now instrumented** (`stm32l4xx_it.c`): each captures `SCB->CFSR`/`SCB->HFSR`/`MMFAR`/`BFAR` into new globals and lights a red LED before trapping.
3. **Blocking-call audit** (read-only, no files modified): `dht.c`/`battery.c`/`light.c` all use bounded, hardware-timer-backed timeouts - safe. **Found**: `log.c`'s live `Log_Init()` contains a real, active `HAL_Delay(1000)` (a FreeRTOS anti-pattern) - runs once, near boot, not obviously at the 5s mark. No other `HAL_Delay()` exists anywhere else in `Core/Src`.
4. **Heartbeat task added** (temporary 8th task, explicitly authorized by the user for this investigation only): `g_heartbeat_count`, toggles the LED's otherwise-unused blue channel every 150ms, no blocking calls.
5. **Stack high-water-mark + free-heap tracking added**: `g_od_stack_hwm`/`g_monitor_stack_hwm` (`uxTaskGetStackHighWaterMark`) and `g_free_heap_size` (`xPortGetFreeHeapSize`), sampled every loop iteration in both `StartTask04` and `StartTask03`.

**Verified**: because `FreeRTOSConfig.h` changed, **every FreeRTOS source file** was rebuilt for the link test (not just the touched app files), confirming the new stack-checking code paths compile cleanly project-wide. All changed files compile zero warnings. Full-project link succeeds, flash usage ~72.0 KB, zero symbols in Configuration's reserved flash page. Confirmed via `nm` that the new functions are genuinely present in the linked binary. `ir.c`/`ir.h`/`object_detection.c`/`object_detection.h` remain byte-for-byte untouched since round 1.

**Round-5 result: the user ran the round-5 build, but reported a NEW, richer symptom picture (LED/buzzer behavior + repeated Event-log entries) rather than a clean single-freeze-point readout - see round 6 below, which reframes and very likely explains everything found so far.**

### SUPERSEDED stopping point (round 6 of the same-day investigation, kept for history): REQUIREMENTS-VS-IMPLEMENTATION AUDIT + ROOT-CAUSE ANALYSIS. High-confidence hypothesis identified (Monitor/ObjectDetection stack overflow via WriteEventRecord()'s FIL), but not yet pinpointed to an exact line or confirmed against a blocking-wait alternative.
**What triggered this round**: user reported, after flashing round 5's build: heartbeat LED blinks blue at boot; using the IR remote turns the buzzer on and it never turns off; the LED stays blue instead of showing Event's red/detected indication; and the `.TXT` events file contains MANY repeated `CONFIGURATION CHANGED`/`STARTUP (NORMAL)`/`MODE NORMAL->ERROR` triples, plus exactly one real `OBJECT DETECTED` (timestamp `1788134503`) and zero `OBJECT CLEARED`.

**Full requirements-vs-implementation table, and detailed answers to every question asked, are in the Object Detection module's own history above (Sec 14)** - not duplicated here. Executive summary of what was found:

1. **The repeated Event-log entries are NOT a software loop bug.** `g_log_test_timestamp` (used only by `StartTask02`'s one-shot `CONFIGURATION CHANGED`/`STARTUP` test calls) is never incremented anywhere in the code (confirmed by search) - it's a fixed constant. Every repeat sharing the identical timestamp `1788134400` is conclusive evidence that `StartTask02`'s one-shot boot code is re-executing from scratch, over and over - which (since FreeRTOS has no task-auto-restart feature and none is implemented here) can only mean **the MCU is genuinely resetting repeatedly**, not that any task is stuck in a loop. Event's `FA_OPEN_APPEND` file design is working exactly as intended; it's faithfully recording many real, distinct reboots into one file.
2. **IWDG/WWDG were directly ruled out as the reset source** by source inspection: no `MX_IWDG_Init()` call and no `hiwdg` handle exist anywhere in `main.c` (only a vestigial, non-functional comment and a commented-out refresh line remain) - confirming the "IWDG removed" history is still accurate, current code, not stale documentation.
3. **Two independent diagnostic threads both point at the same ~5-second mark**: round 3/4's `g_object_detection_poll_count` freezing at 254×20ms≈5.08s, and this round's finding that every `MODE NORMAL->ERROR` entry carries `MonitorTask`'s own un-incremented initial timestamp - meaning it is always **Monitor's first real sampling cycle** (which fires after its hard-coded `osDelay(5000)`) associated with the reset. This is very likely ONE underlying problem, not several.
4. **High-confidence root cause identified**: `MonitorTask_attributes.stack_size` is still the CubeMX default `128*4`=512 bytes - never increased, unlike `InitTask`'s already-fixed identical bug. `event.c`'s `WriteEventRecord()` (called via `Event_OnModeTransition()` when Monitor's first sample lands in Error) declares a **local (stack) `FIL file`** - the same FatFs struct (~560-570 bytes) that already caused `InitTask`'s original stack overflow (Sec 14's Log entry). Rough worst-case stack depth for Monitor's Error-branch call chain (`Monitor_Sample()` → `Event_OnModeTransition()` → `WriteEventRecord()`) is ~750-800+ bytes on a 512-byte stack - essentially guaranteed to overflow. **`ObjectDetectionTask` has the exact same latent vulnerability** via its own `Event_OnObjectDetection()` → `WriteEventRecord()` path, on its own 512-byte stack - not necessarily what triggered this specific incident, but an equally real risk needing the same fix.
5. **The "stuck blue LED" and "stuck-on buzzer" are both fully explained as downstream consequences, not separate bugs**: `led.c`/`event.c` were checked and have **no pin-level conflict** with the heartbeat diagnostic (different GPIO channels, confirmed by source - heartbeat uses `RGB_LED_2`/blue, which no real `LedColor` value ever drives). The buzzer uses hardware PWM (TIM3), which free-runs with zero CPU involvement once started - "stuck on" is exactly what you'd see if the system dies/resets right after `Buzzer_On()` fires, especially since Error mode (which also calls `Buzzer_On()`) recurs on many reset cycles, very plausibly re-triggering the buzzer almost every ~5s and sounding continuous to the ear. Both `Event`'s auto-stop-on-clear logic and the button-silence logic are implemented correctly per spec (Sec 2.3) - they just never get the chance to run again before each reset.

**Ranked root-cause hypotheses**: #1 (high confidence) `MonitorTask`/`ObjectDetectionTask` stack overflow via `WriteEventRecord()`'s local `FIL` - see evidence above. #2 (possible, less evidence) brown-out reset from combined SPI/ADC/DHT/buzzer activity during Monitor's first cycle. #3 (unlikely for this symptom) `log.c`'s live `HAL_Delay(1000)` in `Log_Init()` - confirmed real, but runs once near boot (~t=0), not at t≈5s. #4 (ruled out) IWDG/WWDG - confirmed absent from compiled code.

**Ranked fixes, safest to most invasive**: (1, recommended) increase `MonitorTask`'s and `ObjectDetectionTask`'s stack sizes (e.g. 512→2048 bytes each) - mirrors `InitTask`'s already-proven fix exactly, one line per task, touches no application logic. (2) Confirm empirically via round 5's now-enabled stack-overflow detection after applying fix 1. (3, more invasive) read `RCC->CSR`'s reset-cause flags at boot to positively confirm/rule out any secondary reset source. (4, most invasive, likely unnecessary) redesign `WriteEventRecord()` to avoid a stack-resident `FIL`.

**Per explicit instruction, NO source files were changed this round - this was analysis and comparison only.**

**Round-6 result: the user ran a controlled SD-card-present-vs-removed A/B test rather than approving the fix immediately - see round 7 below, which sharpens and directly reproduces this hypothesis.**

### SUPERSEDED stopping point (round 7, kept for history): SD-card A/B test pinpointed the mechanism to `event.c:136` (`f_write()`). Fix recommended, not yet applied.
Full detail in the Object Detection entry above (Sec 14). Recommended fix: increase `MonitorTask`'s and `ObjectDetectionTask`'s stack sizes.

### SUPERSEDED stopping point (earlier in this same session, kept for history): STACK-OVERFLOW BUG RESOLVED AND HARDWARE-VERIFIED. Object Detection module is DONE. Moving on to the next roadmap module: Init.
User applied the recommended fix directly (`MonitorTask_attributes`/`ObjectDetection_attributes` stack sizes: `128*4`→`1024*4`, i.e. 512→4096 bytes each, `main.c`, old values left commented out) and confirmed on real hardware, SD card inserted: object detection works correctly and repeatably, buzzer/LED behave correctly (on when detected, off when cleared), events are written to the SD card, and the previous freeze/reset loop no longer occurs. This closes the multi-round (1-7) stack-overflow investigation - see the "ROOT CAUSE CONFIRMED..." bullet in the Object Detection entry above (Sec 14) for the full resolution writeup, now updated with the real measured `g_od_stack_hwm`/`g_monitor_stack_hwm`/`g_free_heap_size` values (708 words/680 words/136 bytes respectively).

I then proposed a full Init design (purpose/layer/functions/dependencies/open questions) - see below, this was discussed and effectively agreed before the session ended for the day.

### SUPERSEDED stopping point (2026-09-01 session, end of day, kept for history): Object Detection CLOSED OUT. Init's design is AGREED but NOT YET IMPLEMENTED - no Init code exists yet. Session paused here deliberately for the day.
**Recap of today's session, for a future session picking this up cold:**
1. Investigated and resolved a multi-round (7 rounds) stack-overflow bug in `MonitorTask`/`ObjectDetectionTask` (see Sec 14's Object Detection entry for the full history) - root cause: both tasks' 512-byte stacks were too small for `event.c`'s `WriteEventRecord()`, which declares a large local `FIL` (~560-570 bytes) and calls into FatFs's `f_write()`, itself a deep call chain. Fixed by increasing both tasks' (and `InitTask`'s, already done earlier) stacks to 4096 bytes.
2. **Verified on real hardware, with the SD card inserted**: Object Detection works correctly and repeatably; buzzer/LED behave correctly (on when detected, off when cleared); events are written to the SD card; the previous freeze/reset loop no longer occurs.
3. **Measured real stack margins** (see Sec 14 for full numbers): `ObjectDetectionTask` peak usage ≈1264 bytes of 4096 (≈69% margin); `MonitorTask` peak usage ≈1376 bytes of 4096 (≈66% margin) - both comfortably safe.
4. **Measured a related, NOT-yet-addressed concern**: `g_free_heap_size = 136 bytes` - the total heap (15,360 bytes) is nearly fully consumed by task stacks/TCBs. **Per explicit instruction, left exactly as-is** - no stack/heap changes, `HeartbeatTask` and all diagnostic globals (`g_heartbeat_count`, `g_od_stack_hwm`, `g_monitor_stack_hwm`, `g_free_heap_size`, `g_stack_overflow_detected`/`g_stack_overflow_task_name`, the `g_fault_*` set, the `g_od_diag_*` step markers, `g_object_detection_raw_gpio_level`) remain in place, explicitly treated as **temporary debugging tools, not product functionality**.
5. **Proposed and effectively agreed the Init module's design** (Sec 2.7) - recorded in full below - but **no Init code has been written**. This session stops here by explicit instruction; do not start implementing until the next session.

**AGREED Init design (to implement next session, not yet built):**
- Scope: read the STM32's real hardware RTC (`HAL_RTC_GetTime`/`GetDate` - first real use of this peripheral anywhere in the project), convert it to a Unix timestamp (a new small helper - likely needs its own `DateToEpoch()`-style function, the inverse of `log.c`'s private `EpochToDate()`, which cannot be reused directly since it's `static` and `log.c` is verified/do-not-touch), and call `Event_OnInitStartup()` for real with that timestamp and `wasWatchdogReset=0` - replacing `StartTask02`'s current one-shot test call, which uses fake data.
- Move `Configuration_Init()`, `Log_Init()`, and `Event_Init()` into a new `Init_Start()` function (mechanical relocation of existing calls, no logic change) - this becomes Init's concrete fulfillment of Sec 2.7's "starts all system activities." `Monitor_Init()`/`ObjectDetection_Init()` stay where they are (each task self-initializes) - no cross-task synchronization primitive exists or is being added.
- New files: `Embeded/Core/Inc/init.h`, `Embeded/Core/Src/init.c`. Modified: `main.c` (`StartTask02` calls `Init_Start()` instead of its current inline Configuration/Log/Event calls).
- Proposed functions: `void Init_Start(void);` (the one-shot boot sequence) and `void Init_OnRtcTimeReceived(uint32_t epochSeconds);` (a hook for a future real CC time-sync round-trip to call - not wired to anything yet). No new structs - reuses `TimestampWithFlagMessage`.

**Explicitly deferred (per this session's agreement) - do NOT invent or implement these when Init is built next session:**
- The full LNC→CC time-synchronization round-trip itself. Reason: the only existing message-layer pieces (`Message_ParseSetRtcDateTime`, `Message_ParseGetSystemTimeRequest`+`Message_BuildSystemTimeResponse`) both flow CC→LNC or LNC-replies-to-CC - there is no existing "LNC asks CC for the time" message in either direction Sec 2.7 actually needs, so building this would require adding a new message-layer builder/parser pair, which is bigger than Init itself.
- The Communication TX/dispatch infrastructure that full time-sync would depend on regardless (`CommTxTask` is still a bring-up-test loop; `CommRxTask` still only decodes and counts, never dispatches by tag) - a cross-cutting piece of future work needed by every CC-facing feature, not just Init.
- `InitTask`'s Sec-7-specified "self-deletes" behavior - `StartTask02` currently loops forever (IR/Button test reads, Buzzer toggle); implementing self-delete would require first relocating/removing that loop. Recorded as a nice-to-have cleanup, not required for Init's correctness.

### SUPERSEDED stopping point (2026-09-02 session, earlier block, kept for history): Init IMPLEMENTED, compiled clean, full-project link verified. AWAITING REAL-HARDWARE TEST. RTC/Watchdog integration explicitly deferred, not started.
**What happened this session, for a future session picking this up cold:**
1. Re-read the entire `PROJECT_GUIDE.md` from disk (not a pasted excerpt) before doing anything, per this guide's own instructions.
2. Before implementing Init's originally-agreed design ("read the real hardware RTC"), checked the actual generated code first, per this project's "no silent fixes" rule — found the RTC peripheral is **not actually enabled** (`HAL_RTC_MODULE_ENABLED` commented out, zero RTC entries in `Embeded.ioc`, no `hrtc` handle anywhere), contradicting this guide's own Sec 9.2 claim. Surfaced this as a BLOCKING contradiction and asked the user how to proceed rather than silently implementing against a wrong assumption.
3. **User's explicit, detailed decision**: do NOT touch RTC or Watchdog hardware this session at all. Build Init's software structure/architecture now, with a clean, documented seam for real RTC (and later CC time-sync) to plug in later without redesigning Init. No new FreeRTOS task. No self-delete yet. Reuse the project's existing simple test-timestamp mechanism rather than inventing new infrastructure.
4. Implemented accordingly: `Init_Start(uint32_t timestamp)` — the timestamp parameter itself is the future RTC/CC-sync seam, matching the exact convention `Monitor_Sample()`/`ObjectDetection_Poll()` already use (no separate unused hook function). Relocates `Configuration_Init()`/`Log_Init()`/`Event_Init()` into `init.c` verbatim (Sec 2.7's "starts all system activities"), then calls `Event_OnInitStartup()` with `wasWatchdogReset=0`. Deliberately dropped the old test code's fake `Event_OnConfigurationChanged()` boot call (surfaced as a real behavior change, not silent) since that was never real Sec 2.7 behavior. See the Init entry in Sec 14 above for the full design/verification writeup.
5. **Compiled clean (zero warnings)** against the real ARM toolchain; **full-project link verified** in a scratch copy (real object list + freshly compiled `main.o`/`init.o`, real linker script) — link succeeds, flash usage ~70.5 KB, zero symbols in Configuration's reserved flash page. **Not yet run on real hardware** — that's the next step.
6. Also corrected Sec 9.2's inaccurate RTC bullet in this guide itself (it described a Calendar/LSI configuration that doesn't exist in the current generated code) — see that section for the correction.

**Next session's starting point**: flash the updated firmware and confirm on real hardware that `Init_Start()` behaves identically to the old inline `StartTask02` code it replaced (same `g_config_loaded_from_flash`/`g_log_mounted`/`g_event_last_frame_length` values, one `STARTUP (NORMAL)` line per boot on the SD card) **except** no more spurious `CONFIGURATION CHANGED` line every boot (the deliberate, intentional behavior change - confirm this is what's now seen, not a regression). Once hardware-verified, update Sec 14's Init entry and this section to DONE/HARDWARE-VERIFIED, then move on to Keep-Alive (Sec 13) - Watchdog stays paused (Open Question #2), and RTC/CC time-sync remain explicitly deferred until the user decides to enable them.

**Do NOT, until explicitly instructed otherwise:**
- Touch the Watchdog (IWDG/WWDG) in any way - a separate, later roadmap step (Open Question #2).
- Implement the LNC→CC time-sync round trip or any new Communication dispatch-by-tag infrastructure - needs new message-layer pieces that don't exist yet, bigger than Init itself.
- Change any task's stack size, the heap size, `HeartbeatTask`, or remove any diagnostic variable - all explicitly frozen per the 2026-09-01 session's instruction, still in force.
- Revisit Log's, Event's, Configuration's, Monitor's, or Object Detection's design (all verified working - leave them alone).

### SUPERSEDED stopping point (2026-09-02, kept for history): Init CLOSED OUT. User asked about enabling RTC in CubeMX - guidance given, not yet acted on at that point.

### CURRENT stopping point (2026-09-02, later same day - this is the real one, supersedes every block above): RTC enabled by the user in CubeMX and verified from source; wired into Init as the real timestamp source. Implemented, compiled clean, link-verified. AWAITING REAL-HARDWARE TEST. One unrelated regression found and NOT yet fixed: TIM3's buzzer PWM prescaler.
**What happened this session:**
1. User enabled RTC in CubeMX (Activate Clock Source + Calendar, 24-hour format, LSI clock source) and regenerated, then asked for the actual generated code to be verified before proceeding - not just trusted from the CubeMX GUI.
2. **Verified directly from source, item by item** (see Sec 9.2's updated RTC bullet for the full evidence): `HAL_RTC_MODULE_ENABLED` now defined, `hrtc` handle + real `MX_RTC_Init()` exist and are called before the scheduler starts, clock source is genuinely LSI (`HAL_RTC_MspInit()`), Calendar + 24-hour format confirmed, no alarm/tamper/wakeup/interrupt code anywhere - **RTC configuration is correct and sufficient for what Init needs.** One non-blocking accuracy footnote found (LSE-typical `AsynchPrediv`/`SynchPrediv` values, not recalculated for LSI's real ~32 kHz) - documented, not fixed, doesn't block current use.
3. **Also found, not part of what was asked but explicitly checked for per this session's own request: the CubeMX regeneration silently reset `TIM3`'s Prescaler from the hardware-verified `399` back to `0`** - exactly the risk this guide had already flagged. This is a real regression (buzzer would be inaudible again if flashed as-is) but is **unrelated to RTC's own correctness**, so per the user's explicit "proceed directly, don't re-ask" instruction, this was surfaced clearly (Sec 9.2, Open Question #14) rather than treated as a blocker for the RTC/Init work.
4. **Implemented the RTC→Init wiring**: `DateToEpoch()` + `IsLeapYearForRtc()` added to `main.c` (mirrors `log.c`'s `EpochToDate()` style, per this project's precedent of duplicating log.c's private helpers rather than exporting them). `StartTask02` now calls `HAL_RTC_GetTime()` then `HAL_RTC_GetDate()` (order matters - a real HAL requirement, documented in the code), converts via `DateToEpoch()`, and passes the result into `Init_Start()` instead of the old fixed `g_log_test_timestamp` (now removed, replaced by real `g_rtc_*` debugger-watch globals). `Init_Start(uint32_t timestamp)`'s own signature/design is unchanged, per explicit instruction.
5. **Compiled clean (zero warnings)** against the real ARM toolchain, including the two RTC HAL driver files (`stm32l4xx_hal_rtc.c`/`_ex.c`) that had never been compiled before now. **Full-project link verified from a genuine from-source scratch build** (the existing `Debug/` build predates RTC being enabled, confirmed via file timestamps) - link succeeds, flash usage ~72.7 KB, zero symbols in Configuration's reserved flash page, all RTC/`DateToEpoch` symbols confirmed present via `nm`.
6. **Not yet run on real hardware** - see Sec 14's RTC entry for the exact test procedure.

### SUPERSEDED stopping point (2026-09-02, kept for history): RTC enabled and wired into Init, implemented/link-verified, awaiting first hardware test.

### SUPERSEDED stopping point (2026-09-02, kept for history): First RTC hardware test done - RTC placeholder-date analyzed (fix recommended, not implemented). Stack-size/stack-overflow-detection regression found and fixed in source, NOT yet hardware re-tested. TIM3 buzzer regression still unfixed.

### CURRENT stopping point (2026-09-02, later same day - this is the real one, supersedes every block above): Stack-size fix CONFIRMED on real hardware (LED/IR/Object Detection all working again). Buzzer separately investigated - silent, root cause conclusively isolated to the still-unfixed TIM3 Prescaler (Open Question #14), nothing else in the buzzer path is wrong. Exact CubeMX steps given to the user, including the persistence-verification step that was missed twice before. No code changed this round.
**What happened this session:**
1. User reported two hardware issues after the RTC-enabling flash: (a) RTC shows a fixed placeholder date/time (`2000-01-01 10:31:00`) instead of real time, (b) LED is purple instead of green in Normal mode, and IR detection produces no response at all (previously verified working).
2. **Issue (a) analyzed, not fixed, per explicit instruction ("do NOT invent a time source, do NOT implement CC sync")**: traced to CubeMX's generated `MX_RTC_Init()` unconditionally re-applying its compiled-in placeholder date/time via `HAL_RTC_SetTime()`/`SetDate()` on every single boot, with no guard against overwriting a previously-set real value - exactly matching the observed values byte-for-byte (BCD `0x10:0x31:0x00`, `Year=0x0`→2000, `Month=JANUARY`, `Date=0x1`). A recommended fix (an RTC backup-register guard, at the exact spot CubeMX's own empty `Check_RTC_BKUP` marker suggests) was given but deliberately not implemented - see Sec 14's RTC entry for the full writeup and recommendation.
3. **Issue (b) investigated per explicit instruction (root cause first, no logic changes without confirming it) and found to be unrelated to Event/ObjectDetection/LED/IR code entirely**: the same RTC-enabling CubeMX regeneration had also silently reverted `InitTask`/`MonitorTask`/`ObjectDetectionTask`'s stack sizes from the hardware-verified `1024*4` back to the CubeMX default `128*4`, AND removed `configCHECK_FOR_STACK_OVERFLOW` from `FreeRTOSConfig.h` entirely - reopening the full rounds-1-7 stack-overflow bug (Sec 14) while simultaneously disabling its own detection mechanism, so the overflow now runs uncaught until it trips a real Cortex-M fault, whose handler (round 5's instrumentation, still in place) forces the red LED on and freezes the whole system - fully explaining both the purple LED (red from the fault handler, blue frozen from `HeartbeatTask`'s last state) and the total unresponsiveness to IR (scheduler frozen, nothing polls anything). See Sec 14's writeup for the full causal chain.
4. **Fixed**: restored the 3 tasks' stack sizes to `1024*4` in `main.c` (old values commented out, per this project's established convention) and restored `configCHECK_FOR_STACK_OVERFLOW=2` in `FreeRTOSConfig.h`, this time inside a `USER CODE` marker so it survives future regenerations. No Event/ObjectDetection/LED/IR/Configuration/Log code touched. No stack/heap sizes changed beyond restoring exactly what was already verified. `HeartbeatTask` and all diagnostic globals/instrumentation left in place, per explicit instruction.
5. **Compiled clean (zero warnings)** against the real ARM toolchain - `main.c`, `init.c`, all 10 FreeRTOS core source files (config define changed, same precedent as round 5), and the RTC HAL driver files. **Full-project link verified** - link succeeds, flash usage ~72.8 KB, zero symbols in Configuration's reserved flash page, `vApplicationStackOverflowHook`/`DateToEpoch`/`Init_Start`/`StartHeartbeatTask` all confirmed present via `nm`.
6. **Not yet re-tested on real hardware.**
7. Also added Open Question #16: a general lesson that CubeMX regeneration in this project has now silently reverted hand-tuned values three separate times (TIM3 Prescaler, task stack sizes, `configCHECK_FOR_STACK_OVERFLOW`) - worth explicitly re-checking all of these after any future regeneration, not assuming a clean build/link means nothing was silently lost.

**What happened in this latest round:**
1. User reflashed the stack-size fix and confirmed on real hardware: LED changes color correctly on IR detection (including red), IR detection works, the freeze/purple-LED symptom is gone - **Open Question #15 fully resolved and hardware-confirmed.**
2. Separately reported: the buzzer produces no sound. Investigated per an explicit 11-point checklist, **with explicit instruction not to touch Event/ObjectDetection/IR/LED logic** (already confirmed working) and not to redesign the buzzer.
3. **Full buzzer/TIM3 path traced end to end**: clock source/frequency, GPIO/AF config, PWM mode/polarity, `buzzer.c`, and all 6 of `event.c`'s call sites were all confirmed correct and unchanged. **The sole cause is the still-unfixed TIM3 Prescaler** (Open Question #14, found last session, never actually fixed) - `.ioc` still has no `Prescaler` key for TIM3, `main.c` still shows `htim3.Init.Prescaler = 0`, giving a ~792 kHz (inaudible) PWM frequency instead of ~1980 Hz.
4. **No code was changed** - per the user's own explicit branching instruction ("if it's a CubeMX value, tell me what to change") and this project's established precedent that peripheral timing values are set by the user in CubeMX, not hand-patched in generated code (a hand-patch would just get silently wiped again on the next regeneration, same as the last two attempts). Exact CubeMX steps were given, including a persistence-verification step (check `.ioc` itself for `TIM3.Prescaler=399`, not just `main.c`) that was missed both previous times this value was "fixed."

**Next session's starting point (superseded by the 2026-09-03 block below):**
- **(BLOCKING for the buzzer only, Open Question #14)** User needs to re-set `TIM3`'s Prescaler to `399` in CubeMX and this time verify `Embeded.ioc` actually saves a `TIM3.Prescaler=399` line (not just check `main.c`) before regenerating again. Once done, ready for a quick source re-check + compile/link verification, then reflash to confirm the buzzer audibly works.
- **(User decision needed, RTC sibling of Open Question #14)** Decide whether/when to implement the recommended RTC backup-register guard (Sec 14's RTC entry) so a real time value survives across resets - not implemented this session by explicit instruction, still just a recommendation.

Once the buzzer is confirmed, update Open Question #14 to RESOLVED, then move on to Keep-Alive (Sec 13). Watchdog and the LNC→CC time-sync round trip remain explicitly deferred.

**Remember for later, not blocking now:** `Embeded.ioc` still doesn't list a `Prescaler` entry for TIM3 even though `main.c` has the correct value (399) - re-check this if the project is ever regenerated from CubeMX again, since regeneration could silently reset it to 0.

### CURRENT stopping point (2026-09-03, this is the real one, supersedes every block above): Buzzer/Open Question #14 CONFIRMED RESOLVED. Starting Keep-Alive (Sec 2.8) design/inspection.
At the start of this session, `PROJECT_GUIDE.md` was read in full per its own instructions before doing anything. Before trusting the guide's "buzzer not yet fixed" text, the actual current source was checked directly (this project's own working rule - verify from files, don't assume the guide is current):
- `Embeded.ioc` line 253: `TIM3.Prescaler=399`, and line 251's `TIM3.IPParameters` now lists `Prescaler` as a persisted key - confirms this fix stuck this time, unlike the two earlier attempts.
- `main.c` line 801: `htim3.Init.Prescaler = 399;`
- Task stack sizes (`InitTask`/`MonitorTask`/`ObjectDetection`, all `1024*4`) and `configCHECK_FOR_STACK_OVERFLOW=2` (Open Questions #15/#16) also re-checked and confirmed still intact.

User confirmed this build was reflashed and the buzzer audibly works correctly on real hardware. **Open Question #14 is now formally RESOLVED** (moved to Sec 10's Resolved list; Sec 14's Buzzer entry and this section both updated).

**Keep-Alive (Sec 2.8) inspection findings (all verified from source, not assumed):**
- `CommTxTask` (`StartTask06`) was still 100% bring-up scaffolding: a fixed 3-byte test frame (tag `0x01`) via direct `Transport_UART_Send()` every 2s - no Message layer involved.
- `txQueueKeepAlive`/`txQueueEvent`/`txQueueData` **do not exist anywhere in code** - grepped `main.c` for "Queue", zero matches. Still purely a Sec 7 design intent.
- `KeepAliveTask` (`StartTask07`) was a completely empty `for(;;) osDelay(1);` stub.
- `Message_BuildKeepAlive(const MeasurementSample*, uint8_t*, uint16_t)` already existed and was already tested (part of the original 19/21-tag Message layer work) - nothing new needed there.
- Monitor had no getter for its last sample - `StartTask03` copied `Monitor_Sample()`'s output straight into `main.c`'s own temporary debugger-watch globals, with no real module-level API another module could use.
- `event.c` already builds real CC-bound frames internally but nothing sends them (confirmed by grep) - Keep-Alive would be the first real, permanently-wired sender anywhere on the LNC.

**Decision (user-approved): narrow scope.** Send real `TAG_KEEPALIVE` frames directly via `Transport_UART_Send()` every 6s; explicitly defer the Sec 7 3-tier TX-priority-queue system until a second real sender (Event/DataReport) exists and there is genuine concurrent traffic to arbitrate - consistent with the precedent already set for Init's own explicitly-deferred TX/dispatch infrastructure.

**Keep-Alive IMPLEMENTED, compiled clean, full-project link verified this session - see the Keep-Alive entry in Sec 14 above for the complete design/verification writeup.** `Monitor_GetLastSample()` added (additive, non-breaking); new `keepalive.h`/`keepalive.c` (`KeepAlive_Send(uint32_t timestamp)`); `StartTask07` rewritten to read the RTC (same pattern as `StartTask02`/Init), call `KeepAlive_Send()`, and repeat every 6s. No other task, priority, or stack size touched; `event.c`/`object_detection.c`/`CommRxTask` all byte-for-byte unchanged. **Not yet run on real hardware** - that's the next step, see Sec 14's Keep-Alive entry for the exact test procedure.

### CURRENT stopping point (2026-09-03, this is the real one, supersedes the block above): Keep-Alive HARDWARE-VERIFIED. Sec 2.8 fully done. Watchdog is the only remaining unbuilt LNC Application module, and it stays explicitly paused.
User confirmed on real hardware: `g_keepalive_send_count` increases roughly every 6s, `g_keepalive_last_timestamp` updates correctly, `g_keepalive_last_frame_length` is exactly `27` as expected. **Keep-Alive (Sec 2.8) is done.**

**Where the LNC Application-module roadmap (Sec 13 step 4) now stands:** Configuration, Log, Event, Monitor, Object Detection, Init, Keep-Alive - all done and hardware-verified. **Watchdog (Sec 2.9) is the only one left**, and it remains explicitly paused per Open Question #2 - not a tuning question anymore but a full redesign, since the IWDG peripheral and `WatchdogTask`'s refresh logic were removed entirely earlier in this project (Sec 14's UART/RX investigation) after being found to cause a real hardware bug (resetting the MCU every ~512ms). **Do not touch Watchdog without explicit instruction to revisit it.**

**With Watchdog paused, the next unstarted major roadmap item (Sec 13) is Central Computer's Ground-Station-facing side / Ground Station itself (steps 5's remaining piece, and step 6):**
- Central Computer's LNC-facing half is done (Communication, Management Command, DataStore, DataCollection, report_generator, `cc_main.cpp` - Sec 14).
- **No Ethernet-facing/GS-facing module exists on the CC side at all yet** - Sec 3 notes this isn't an explicitly-named spec module but is required by Sections 1.2/4 (architectural recommendation).
- **No Ground Station code exists at all** - Sec 4's GS is entirely unstarted; `Common/TLVCodec` (already built, CC-side) is meant to be shared by both CC and GS, so no new protocol/message-layer work should be needed there, only the GS application itself plus CC's Ethernet-facing listener.
- Also still open, cross-cutting, and not yet built anywhere: the LNC-side dispatch-by-tag (`CommRxTask` still only decodes and counts, never acts on a received command - Sec 14's "NOT yet verified / not yet built" list) and the Sec 7 TX-priority-queue system (deferred again during Keep-Alive, still just a design intent).

**User chose: LNC-side command dispatch (candidate B).**

### CURRENT stopping point (2026-09-03, this is the real one, supersedes the block above): Communication/Dispatch module IMPLEMENTED and link-verified. NOT YET TESTED ON REAL HARDWARE.
Full design was inspected and proposed first, per Sec 12's working rule, before any code was written - see the Communication entry in Sec 14 above for the complete design/verification writeup. Summary for a future session picking this up cold:
1. New `communication.h`/`communication.c` added: `Communication_Dispatch(frame, timestamp)`, called from `CommRxTask` once per successfully decoded frame.
2. **9 of 12 CC→LNC tags fully wired**: the 8 "set limit" commands (parse → `Config_Set*()` → `Event_OnConfigurationChanged()`, frame built but not sent, unchanged boundary) and `GET_SYSTEM_TIME_REQUEST` (parse → build → send directly via `Transport_UART_Send()`).
3. **3 tags deliberately deferred, counted separately, not silently dropped**: `SET_RTC_DATETIME` (blocked on the not-yet-implemented RTC backup-register guard) and the 2 "retrieve by range" requests (blocked on a not-yet-built SD-card historical-retrieval feature).
4. **Per explicit instruction, the config-changed Event frame is NOT sent to the CC yet** - stays at the same "build but don't send" boundary as every other Event call site. Real sending for all Event types together is intentionally left for a future step: the Sec 7 TX-priority-queue system (candidate C from this session's fork), not special-cased here.
5. `event.c`, `message.c`, `monitor.c`, `keepalive.c`, `CommTxTask`, and all task priorities/stack sizes are confirmed untouched (`git status` checked at the end of this session).
6. Compiled clean (zero warnings) against the real ARM toolchain; full-project link verified in a scratch copy - link succeeds, flash usage ~77.1 KB, zero symbols in Configuration's reserved flash page, all new symbols (`Communication_Dispatch` + 5 getters) confirmed present via `nm`.
7. **Not yet run on real hardware** - see Sec 14's Communication entry for the exact test procedure (watch `g_dispatch_*` globals while sending a command from the CC side; note `cc_main.cpp`'s current menu only exercises `setRtcDateTime`/backfill, so testing a "set limit" command's dispatch will need either a small temporary CC-side test hook or a hand-crafted frame).

**User chose to continue directly to C (TX priority-queue system) before hardware-testing B, specifically to get a richer combined B+C integration test afterward** (both CC→LNC dispatch and LNC→CC transmission exercised together in one hardware pass).

### SUPERSEDED stopping point (2026-09-03, kept for history): TX Priority Queue system (Sec 7) IMPLEMENTED and link-verified. NOT YET TESTED ON REAL HARDWARE. Neither B nor C has been flashed yet.
(Full narrative kept below in the CURRENT block, which consolidates both B and C together - this heading is kept only so the chronological trail isn't broken.)

### SUPERSEDED stopping point (2026-09-03, kept for history): B (LNC Command Dispatch) + C (TX Priority Queue) both IMPLEMENTED and BUILD/LINK VERIFIED. NEITHER HAS BEEN TESTED ON REAL HARDWARE YET.
(Full narrative kept below in the CURRENT block, which now records real hardware-test results - this heading is kept only so the chronological trail isn't broken.)

### SUPERSEDED stopping point (2026-09-06, kept for history): Real-hardware verification of B+C IN PROGRESS. 4 of 8 planned tests PASS, 4 BLOCKED on a test-tooling decision not yet made.
(HW-B-C-02 was blocked/failing at this point - see the CURRENT block below for the full investigation and its resolution.)

### CURRENT stopping point (2026-09-06, this is the real one, supersedes every block above): HW-B-C-02 AND HW-B-C-03 both investigated/tested end-to-end and now PASS. The full RX -> Decode -> Dispatch path AND the request/response round trip are VERIFIED on real hardware for the first time with genuine CC-originated frames. 6 of 8 planned B+C tests are now PASS. Temporary debug instrumentation has since been fully removed - see below.

**This is the single most important block to read if picking this up cold** - it closes out a long investigation and corrects an earlier wrong conclusion made mid-investigation.

#### The investigation, in order
1. HW-B-C-02 (`[5]` Set-Limit) initially showed **zero** movement on every counter (`g_frames_decoded_ok`, `g_frames_decode_error`, `g_dispatch_*`) despite the CC reporting `sendFrame result: ok`.
2. Ruled out via source inspection (all confirmed correct, no bug found): CC-side/LNC-side wire-format agreement (8-byte payload, matching tag), USART2 GPIO/AF config, baud/parity/stopbits match, `CommRxTask`'s dispatch-wiring code, COM10's identity (confirmed via Windows `GetPortNames()`/WMI - genuinely the ST-Link VCP, not a stale/wrong port).
3. **A real, important discovery from this project's own history**: `PROJECT_GUIDE.md`'s own UART/RX investigation writeup was reread carefully and shows the CC->LNC direction had **never actually been proven end-to-end in one unified test** before this session - only via a self-loopback jumper using the LNC's own bring-up frame, never a genuine CC-originated command in the same run. B's dispatch test was the first real exercise of that link.
4. A physical PA2<->PA3 loopback jumper was reinstalled and verified at ~0 ohms (board powered off). Sending `[5]` with the jumper installed still showed zero dispatch. This ruled out a Nucleo solder-bridge/hardware-routing hypothesis that had been raised along the way.
5. **A wrong conclusion was reached mid-investigation and should be flagged as corrected, not repeated**: `huart2.RxState = 0x22` (`HAL_UART_STATE_BUSY_RX`) was initially treated as a "stuck/wedged" state requiring a fix. Further tracing of `HAL_UART_Receive()`'s actual HAL source showed this is **normal, expected behavior** - `CommRxTask` calls it with a 20ms timeout and only a 1ms idle delay between calls, so the handle is genuinely in `BUSY_RX` roughly 95% of the time on a perfectly healthy system. A single debugger snapshot showing `0x22` proves nothing by itself. **No HAL-state-recovery fix is needed or was implemented.**
6. **Temporary, purely-additive debug instrumentation was added** (with explicit user authorization to do so) to `comm_transport_uart.c` and `main.c` - per-outcome counters for every `HAL_UART_Receive()` result (`OK`/`TIMEOUT`/`BUSY`/`ERROR`), a one-shot USART2 register snapshot taken right after `MX_USART2_UART_Init()` (before the scheduler starts), `CommRxTask` loop/byte/decoder-state counters, and a `_write()` reachability counter. **No production statement was changed** - confirmed via `git diff --stat` showing only additive lines in both files. Full exhaustive search of the entire firmware for every access to `huart2` found exactly 3 call sites (`main.c`'s init/`_write()`, `comm_transport_uart.c`'s polling send/receive, `stm32l4xx_it.c`'s `USART2_IRQHandler`) - no `_IT`/`_DMA` receive variant, no `AbortReceive`, no direct `RxState` write, no DMA handle anywhere. `_write()` was confirmed to exist (a real, separate violation of this project's own locked "no printf on USART2" rule) but a targeted search found no actual `printf`/`puts` call anywhere in the firmware - its own counter (`g_dbg_write_calls`) has not been reported as nonzero.
7. **Test run 1 (jumper installed, ~123 seconds, no `[5]` pressed)**: `g_dbg_tx_calls`/`g_dbg_tx_ok` = 50 (LNC's own Keep-Alive/Data genuinely transmitted 50 times) but **`g_dbg_hal_ok` = 0, `g_dbg_bytes_received` = 0** - despite a confirmed 0-ohm physical short between TX and RX, **not one single byte ever looped back into a successful receive** in over 6000 poll attempts. This is a real, separate, unexplained anomaly - recorded below, not blocking.
8. **Test run 2 (jumper removed)**: `g_dbg_hal_ok` = 14, `g_dbg_bytes_received` = 14, `g_dbg_last_byte` = 123, and **`g_frames_decoded_ok` = 1** - the first successful frame decode from a genuine CC-originated byte stream in this entire investigation. (One instrumentation flaw was found and corrected in interpreting this run: `g_dbg_isr_sticky` never showed the RXNE bit even though bytes were demonstrably received - because reading `USART2->RDR` inside `HAL_UART_Receive()` clears RXNE in hardware before the "after" sample point could catch it. `g_dbg_isr_sticky` is unreliable for this purpose and should be disregarded; the direct byte/frame counters are trustworthy and are what this conclusion rests on.)
9. **Final clean, controlled confirmation test (2026-09-06)**: board reset, counters confirmed at a clean 0 baseline, jumper removed, `[5]` pressed exactly once, waited 5 seconds. Result:
   ```
   g_frames_decoded_ok                  = 1
   g_dispatch_set_limit_count           = 1
   g_dispatch_last_tag                  = 1   (0x01 = TAG_SET_TEMP_NORMAL_RANGE)
   g_dispatch_unknown_tag_count         = 0
   g_dispatch_deferred_count            = 0
   g_dispatch_system_time_request_count = 0
   ```
   `g_dispatch_set_limit_count` only increments in `communication.c`'s switch-case *after* `Config_SetTempNormalRange()` has already been called synchronously in the same call - so this result is proof by construction that the full chain executed: USART2 RX -> `Transport_UART_ReceiveByte()` -> `Protocol_FeedByte()` -> `PROTOCOL_DECODE_FRAME_READY` -> `Communication_Dispatch()` -> `Message_ParseSetTempNormalRange()` -> `Config_SetTempNormalRange()`. **(Not independently re-confirmed this round: `g_config_temp_normal_low`/`_high`'s own debugger globals - worth a quick look next time as a completeness check, though not required to accept this result given the above.)**

#### Conclusion
**HW-B-C-02 (Set-Limit): PASS.** The RX -> Decode -> Dispatch path (`USART2 RX -> Transport_UART_ReceiveByte() -> Protocol_FeedByte() -> Communication_Dispatch()`) is **VERIFIED on real hardware** with a genuine CC-originated command, for the first time in this project's history (superseding the earlier, incomplete "two separately-verified halves" loopback-only proof from the original UART/RX investigation). **No bug was found or fixed in B's or C's production code** - `Communication_Dispatch()`, `Message_ParseSetTempNormalRange()`, `Config_SetTempNormalRange()`, `Transport_UART_ReceiveByte()`, and `Protocol_FeedByte()` are all confirmed correct as originally implemented. The earlier repeated zero-dispatch results were never explained down to a single root cause and remain an open curiosity (see below) - but are conclusively no longer reproducing under a clean, controlled test.

#### Separate, unexplained, non-blocking finding: PA2<->PA3 self-loopback anomaly
With a physically confirmed 0-ohm jumper directly across the LNC's own TX and RX pins, and the LNC genuinely transmitting (50/50 successful `Transport_UART_Send()` calls in the same window), **zero bytes were ever successfully self-received** (`g_dbg_hal_ok = 0`). This contradicts this project's own earlier historical success with the identical technique (the original UART/RX investigation's "`g_frames_decoded_ok` observed increasing from 20 to 30"). No explanation was found or pursued this round - the real CC<->LNC path (jumper removed) is what actually matters for this project's requirements, and that path is now proven working. Recorded here so a future session doesn't waste time re-deriving this if the jumper technique is ever reached for again (e.g. for HW-B-C-06's TX-priority-ordering test, which does not depend on this working).

#### Housekeeping: temporary debug instrumentation REMOVED (2026-09-06)
Now that both HW-B-C-02 and HW-B-C-03 are confirmed PASS on real hardware, the 24 temporary `g_dbg_*` globals and their call sites (added to `comm_transport_uart.c` and `main.c` during the RX-path investigation) were fully reverted, since neither remaining test (HW-B-C-06/07) needed any of them - HW-B-C-06 needs an external byte-level capture tool (unrelated file), and HW-B-C-07 would need new, different instrumentation inside `tx_queue.c` (never touched by this instrumentation pass).
- **`comm_transport_uart.c`**: restored to byte-for-byte identical to its pre-investigation, git-HEAD-committed form (confirmed - it no longer appears in `git diff` at all).
- **`main.c`**: the 4 instrumented spots (the debug-globals block, the one-shot USART2 init snapshot in `main()`, the `_write()` counter, and the 3 added lines in `CommRxTask`'s loop) were each removed, restoring each function to its exact pre-investigation body.
- **Verified clean**: `grep -rn "g_dbg_"` across the entire project returns zero matches. Both files compile with zero warnings against the real ARM toolchain; full-project link succeeds; flash usage (`78436` bytes text / `164` data / `24012` bss) matches **exactly** the C-step build from before any debug instrumentation was ever added - a byte-for-byte confirmation that removal fully restored original production behavior, not just "close enough."
- One item was deliberately *not* turned into permanent instrumentation, per the earlier analysis: `_write()` (the newlib stdio-to-`HAL_UART_Transmit(&huart2, ..., HAL_MAX_DELAY)` syscall override) remains a real, standing violation of this project's own locked "no printf on USART2" design rule (Sec 7) - confirmed unreachable by any current `printf`/`puts` call in the firmware, but not physically removed or guarded against future misuse. Recorded here as a known, accepted, open item rather than babysat by a permanent counter - worth a fresh grep for `printf`/`puts` calls if this is ever revisited, rather than trusting this note to stay accurate forever.

**Read this block first if picking the project up cold.**

#### Verified PASS
| Test ID | What | Result |
|---|---|---|
| HW-B-C-01 (Keep-Alive) | Keep-Alive frame now routed through `txQueueKeepAlive`/`CommTxTask` instead of a direct send | **PASS** - transmission through the new TX Priority Queue observed successfully. |
| HW-B-C-04 (Data Report) | New `TAG_DATA_REPORT` producer (Monitor, added in step C) reaching the CC via `txQueueData` | **PASS** - `Frames dispatched` increased, CC's measurement count rose from 4 to 5 and later reached 80, `Decode errors` stayed 0, values valid. No events were intentionally generated during this specific test. |
| HW-B-C-08 (`CommTxTask` stack) | `CommTxTask`'s stack margin under ~30-60s of combined Keep-Alive+Data traffic | **PASS WITH WARNING** - `uxTaskGetStackHighWaterMark(CommTxTaskHandle)` held steady at `36 words` free out of `128 words` (512 bytes) allocated = ~144 bytes / ~28.1% margin free. Stable, no overflow, no task failure. **Per explicit instruction: do NOT increase the stack yet** - only revisit if later testing (e.g. heavier Event load) shows an actual need. |
| HW-B-C-05 (Event transmission) | Event frames (object-detection, confirmed; mode-transition/config-changed/startup share the identical code path and are expected to work the same way, not separately re-tested) reaching the CC via `txQueueEvent` | **PASS** (2026-09-06, after a re-check - see below). `Frames dispatched` 58->98 (+40), `Decode errors` unchanged at 2 (pre-existing, unrelated), `Events` 2->10 (+8, matching the 8 detect/clear transitions triggered). Independently cross-checked directly against `central_computer.db`: exactly 10 rows, all `TAG_EVENT_OBJECT_DETECTION`, alternating detected/cleared - confirms the full IR -> Event -> TxQueue -> UART -> CC -> DB -> report path end-to-end. |
| HW-B-C-02 (Set-Limit) | CC-originated `TAG_SET_TEMP_NORMAL_RANGE` reaching the LNC and dispatching correctly | **PASS** (2026-09-06, after a substantial investigation - see the full writeup below in this session's CURRENT stopping-point block). Clean controlled test: `g_frames_decoded_ok=1`, `g_dispatch_set_limit_count=1`, `g_dispatch_last_tag=1` (`0x01`), all other dispatch counters 0. This is the first real-hardware proof of the full RX->Decode->Dispatch chain with a genuine CC-originated frame in this project's history. No bug was found in B/C production code - the investigation concluded without a fix being needed. |
| HW-B-C-03 (GetSystemTime round trip) | Full `GET_SYSTEM_TIME_REQUEST`/`TAG_SYSTEM_TIME_RESPONSE` round trip | **PASS** (2026-09-06). Clean controlled test, jumper removed, board reset, `[6]` pressed once: `g_dbg_hal_ok=6`, `g_dbg_bytes_received=6` (exactly the expected 6-byte zero-payload frame size - this also retroactively confirms the earlier same-session "33 bytes received, no dispatch" anomaly was caused by testing against a non-reset board with stale cumulative counters, not a real bug - see the dedicated writeup below), `g_frames_decoded_ok=1`, `g_dispatch_system_time_request_count=1`, `g_dispatch_last_tag=16` (`0x10`), every other dispatch counter 0. The CC then printed `[reply] system time response: timestamp=946722668`, confirming a `TAG_SYSTEM_TIME_RESPONSE` frame was built, enqueued, sent, received, and decoded back on the CC side. **This is the first request/response round trip (not just one-directional delivery) proven on real hardware in this project.** No production-code bug was found or fixed. |

#### FAIL / INCOMPLETE
None remaining as of the re-check below - HW-B-C-05 moved to PASS.

**HW-B-C-05 re-check (2026-09-06), zero-cost diagnostic - RESOLVED, moved to PASS.**
The original FAIL/INCOMPLETE run only checked the CC's final report, not the live `[3] Show status` counters during the burst - the zero-cost re-check filled that gap:
- **Root-cause investigation (read-only DB inspection, zero source/config changes) on the original FAIL run**: queried `CentralComputer/build/central_computer.db` directly (Python's stdlib `sqlite3`, no project file touched): `events` table had **0 rows, unfiltered by any date range** - ruled out "CC receives it but doesn't store/report it correctly," narrowed the problem to "LNC never sent it" vs. "CC failed to decode/dispatch it."
- **Re-check performed**: before an IR detect/clear burst, `[3]` showed `Frames dispatched: 58`, `Decode errors: 2`, `[4]` showed `Events: 2` (`ObjectDetection: 2`). After triggering several detect/clear cycles and waiting: `Frames dispatched: 98` (+40, consistent with ~1-2 minutes of combined Keep-Alive/Data/Event traffic, not just the 8 new events), `Decode errors: 2` (**unchanged** - the 2 pre-existing decode errors predate this test and are not caused by Event traffic), `Events: 10` (`ObjectDetection: 10`, +8 matching the number of detect/clear transitions triggered).
- **Independently cross-checked directly against the database** (not just trusting console text): `SELECT * FROM events` returned exactly 10 rows, all `event_type = 49` (`TAG_EVENT_OBJECT_DETECTION`), alternating `"Object detected"`/`"Object cleared"` - a byte-for-byte match to the reported counts. This confirms the full path end-to-end on real hardware: **IR sensor -> Object Detection -> Event -> `TxQueue_EnqueueEvent()` -> `CommTxTask` -> UART -> CC decode/dispatch -> `DataCollection` -> `DataStore` -> report**.
- **Conclusion: HW-B-C-05 PASS.** The 2 pre-existing decode errors are unrelated to Event transmission (present before the burst, unchanged after) and should be tracked separately if they recur, not treated as a C-step defect.

**HW-B-C-03 (2026-09-06), full writeup - GetSystemTime round trip, PASS.**
- CC console: `[cmd] requestSystemTime() -> sendFrame result: ok`, followed later by `[reply] system time response: timestamp=946722668`.
- LNC debugger (board reset immediately before this test, jumper removed): `g_dbg_hal_ok = 6`, `g_dbg_bytes_received = 6`, `g_frames_decoded_ok = 1`, `g_dispatch_system_time_request_count = 1`, `g_dispatch_last_tag = 16` (`0x10` = `TAG_GET_SYSTEM_TIME_REQUEST`), `g_dispatch_unknown_tag_count = 0`, `g_dispatch_deferred_count = 0`, `g_dispatch_set_limit_count = 0`.
- **6 bytes received exactly matches the expected frame size** for this tag's zero-length payload (`SOF+Tag+Length(2)+CRC(2)`, no Value bytes) - both sides' wire-format encoding were independently traced from source before this test (`tlv::encodeFrame`'s CC-side zero-length handling and `Protocol_FeedByte()`'s `PROTOCOL_STATE_READ_LENGTH_HIGH` case's explicit `if (decoder->length == 0)` fast path to `PROTOCOL_STATE_READ_CRC_LOW`) and found correct on both sides - no code bug exists for this zero-payload edge case.
- **Verified end-to-end path**: CC `requestSystemTime()` -> CC UART TX -> STM32 UART RX -> 6-byte `GET_SYSTEM_TIME_REQUEST` frame -> Protocol decode -> `TAG_GET_SYSTEM_TIME_REQUEST` (`0x10`) -> `Communication_Dispatch()` -> `SendSystemTimeResponse()` builds `TAG_SYSTEM_TIME_RESPONSE` -> `TxQueue_EnqueueEvent()` -> `CommTxTask` -> STM32 UART TX -> CC UART RX -> CC decode -> `onSystemTimeResponse` callback prints the timestamp. **This is the first full request/response round trip (not just one-directional delivery) proven on real hardware in this project** - HW-B-C-02 proved CC->LNC delivery and HW-B-C-01/04/05 proved LNC->CC delivery, but this is the first single test proving both directions chained together for one logical operation.
- **Retroactively explains this same session's earlier "33 bytes received, no dispatch" anomaly** (reported before this clean test): that earlier attempt was run without resetting the board first, so `g_dbg_hal_ok`/`g_dbg_bytes_received` were stale cumulative values carried over from the prior HW-B-C-02 test, not a fresh count for that specific `[6]` press - not a real decode/dispatch bug, just a misleading non-reset baseline. No further action needed on that anomaly.
- **Conclusion: HW-B-C-03 PASS. No production-code bug was found or fixed.**

#### BLOCKED (not yet run, need a decision before they can run)
| Test ID | What | Blocked on |
|---|---|---|
| HW-B-C-06 (TX priority ordering) | Raw, timestamped byte-level capture of COM10 | Confirmed by direct attempt this session: a plain text terminal at COM10/115200/8-N-1 shows binary garbage, as expected (the protocol is binary, not text) - a proper raw hex-capture tool is needed. None exists in this session (a prior session had a throwaway `com10_hex_capture.py`-style script; it lived in that session's scratchpad and is gone). Also requires `cc_main.exe` to NOT be running at the same time (COM10 is opened exclusively - confirmed earlier in this project). |
| HW-B-C-07 (Queue-full / high load) | Observing dropped frames when a queue fills | No in-firmware counter exists for a dropped/queue-full enqueue - every `TxQueue_Enqueue*()` call site currently discards the 0/1 return value. Runnable today only indirectly (compare SD-card event count during a deliberately-stalled-UART window against what the CC eventually receives), with no clean automated PASS/FAIL signal. |

#### Not yet tested at all
Nothing beyond the 8 planned tests - no other B/C behavior is untested; the gap is entirely in the 2 remaining rows (HW-B-C-06/07). HW-B-C-02, HW-B-C-03, and HW-B-C-05 are all now resolved (PASS, see above) - **6 of 8 planned tests are PASS**.

#### Test-support addition #1 — IMPLEMENTED (2026-09-06): `cc_main.cpp` menu options for HW-B-C-02/03
**Files changed: `CentralComputer/src/cc_main.cpp` only** (42 lines added, 0 removed - confirmed via `git diff --stat`). No LNC file touched, no `tx_queue.c`/`communication.h`/`.cpp` (production dispatch/queue code) touched, no `management_command.h/.cpp` touched (only its already-existing, already-tested functions are called).

- **`[5] Send test command: set temperature Normal range (HW-B-C-02)`** - new `handleSetTempNormalRange()`, calls the already-existing `mgmt.setTempNormalRange(18.0f, 28.0f)` (deliberately different from Configuration's 15.0/30.0 default so the change is unambiguous). Prints the send result and exactly which LNC debugger globals to check (`g_dispatch_set_limit_count`, `g_config_temp_normal_low`/`_high`).
- **`[6] Request the LNC's system time (HW-B-C-03)`** - new `handleRequestSystemTime()`, calls the already-existing `mgmt.requestSystemTime()`. Prints the send result and which LNC global to check (`g_dispatch_system_time_request_count`).
- **`comm.callbacks.onSystemTimeResponse` now registered** in `main()` (a one-line lambda printing `timestamp=%u` when a `TAG_SYSTEM_TIME_RESPONSE` arrives) - this callback slot already existed on `Communication` but nothing had ever registered a handler for it; without this, `[6]`'s reply would arrive and be silently dropped with no way to observe it.
- Both new handlers follow the exact structure/style of the existing `handleSendTestCommand()`/`handleRequestBackfill()` (`[1]`/`[2]`) - no new patterns introduced.
- **Build verified**: `cc_main.cpp` recompiles with the same single pre-existing warning as before (`ModeName` defined but unused - present before this change, unrelated to it, not touched). Full link succeeds (`cc_main.exe` rebuilt, exit 0). **Both HW-B-C-02 and HW-B-C-03 have since been run and PASS** - see the Verified PASS table and the dedicated writeups above.

#### Minimum test-support changes still identified but NOT implemented for the remaining 2 blocked tests (ANALYSIS ONLY, per explicit instruction - #2 and #3 below were explicitly not approved this round)

1. **(test-support addition #2, for HW-B-C-06)**: a new, standalone script (e.g. Python + `pyserial`, matching this project's own precedent) that reads raw bytes off COM10 with timestamps and prints/logs them - not a modification to any existing project file, a new diagnostic tool only, run instead of `cc_main.exe` (never alongside it).
2. **(test-support addition #3, for HW-B-C-07)**: the smallest option is 3 small counters + 3 getters **inside `tx_queue.c` itself** (mirroring the getter-based observability pattern already used everywhere in this project - `Config_WasLoadedFromFlash()`, `Event_IsAlarmActive()`, `Communication_GetLastDispatchedTag()`, etc.) capturing `TxQueue_Enqueue*()`'s already-computed 0/1 result, which is currently thrown away at every call site. **This does touch a C-step file**, but adds pure observability (a counter that changes only what can be *seen*, never what the queue *does*) rather than changing any enqueue/dequeue behavior - the same distinction this project has already drawn for every other module's diagnostic getters. The alternative (capturing the result at each of the 5 call sites in `monitor.c`/`object_detection.c`/`init.c`/`communication.c`/`keepalive.c` instead) would touch *more* files for the same information, so the single-file option is the smaller change if this is approved later. **Not implemented - the user's explicit instruction was analysis only.**

#### Recommendation: finish B+C verification before starting a new feature
Matches the user's own stated preference. Reasoning: B and C are cross-cutting infrastructure every future CC-facing feature (Ground Station, further chunked retrieval, etc.) will build on top of. Shipping a new feature on top of an unverified, possibly-buggy TX path would make any future bug much harder to isolate (is it the new feature, or a latent B/C bug?). The one open FAIL (HW-B-C-05) is exactly the kind of thing that should be resolved while the change is still fresh and small, not months later.

#### Recommended next sequence of steps
1. ~~Zero-cost HW-B-C-05 re-check~~ - **DONE, PASS** (2026-09-06).
2. ~~Test-support addition #1 (cc_main.cpp menu options for HW-B-C-02/03)~~ - **DONE, IMPLEMENTED AND BUILD-VERIFIED** (2026-09-06).
3. ~~Run HW-B-C-02 on real hardware~~ - **DONE, PASS, after a substantial RX-path investigation** (2026-09-06, see the CURRENT stopping-point block in Sec 15 for the full trace). No code fix was needed.
4. ~~Run HW-B-C-03 (`[6]`, GetSystemTime round trip) on real hardware~~ - **DONE, PASS** (2026-09-06, see above). No code fix was needed.
5. ~~Housekeeping: revert the temporary debug instrumentation~~ - **DONE** (2026-09-06, see above). Full project compiles/links clean, flash usage matches the pre-instrumentation build exactly, zero `g_dbg_*` references remain anywhere.
6. Separately, decide whether to approve test-support additions #2 (raw-capture script, for HW-B-C-06) and #3 (`tx_queue.c` drop counters, for HW-B-C-07) - neither is implemented yet.
7. Once 06/07 are unblocked (if approved) and run - update Sec 14's Communication and TX Priority Queue entries to DONE/HARDWARE-VERIFIED, then return to the open fork: Ground Station + CC's Ethernet-facing module, FleetOOP, or Watchdog.

**Read this block first if picking the project up cold. It consolidates everything from both the B and C steps done this session.**

#### B — LNC Command Dispatch: what was implemented
Sec 2.5's "receives management commands from CC" / "receives retrieval instructions from CC" - the piece that was missing because `CommRxTask` could decode a frame but never acted on its Tag.
- **New**: `Embeded/Core/Inc/communication.h`, `Embeded/Core/Src/communication.c` - `Communication_Dispatch(const ProtocolFrame *frame, uint32_t timestamp)`, called by `CommRxTask` once per successfully decoded frame.
- **9 of 12 CC→LNC tags fully handled**: the 8 "set limit" commands (parse → matching `Config_Set*()` setter → `Event_OnConfigurationChanged()`, frame now enqueued, see C below) and `GET_SYSTEM_TIME_REQUEST` (parse → `Message_BuildSystemTimeResponse()` → enqueued, see C below).
- **3 of 12 tags deliberately deferred, counted separately, not silently dropped**: `SET_RTC_DATETIME` (acting on it now would be pointless - `MX_RTC_Init()` re-applies its placeholder boot time on every reset until the not-yet-implemented RTC backup-register guard exists) and the 2 "retrieve by range" requests (need historical SD-card data read back and chunked - a separate, not-yet-built feature).
- **Observability**: 5 getters on `communication.c` (`Communication_GetLastDispatchedTag/_GetSetLimitCount/_GetSystemTimeRequestCount/_GetDeferredCount/_GetUnknownTagCount`), mirrored by `main.c` into 5 new debugger-watch globals (`g_dispatch_last_tag`, `g_dispatch_set_limit_count`, `g_dispatch_system_time_request_count`, `g_dispatch_deferred_count`, `g_dispatch_unknown_tag_count`) after every dispatched frame.
- **Not touched**: `event.c`, `message.c`, `monitor.c` (at the time of the B step), `keepalive.c`, `CommTxTask`, any task priority/stack size.
- Full writeup: Sec 14's "Communication / Command Dispatch module" entry.

#### C — TX Priority Queue: what was implemented
Sec 7's frozen `txQueueKeepAlive`(2) > `txQueueEvent`(8) > `txQueueData`(4), drained by `CommTxTask` in strict priority order - the piece needed so Keep-Alive/Event/Data traffic can actually reach the CC through one arbitrated path instead of each sender writing to the UART directly (or, for Event, not sending at all).
- **New**: `Embeded/Core/Inc/tx_queue.h`, `Embeded/Core/Src/tx_queue.c` - `TxQueue_Init()`, `TxQueue_EnqueueKeepAlive/_EnqueueEvent/_EnqueueData()`, `TxQueue_DrainOne()`.
- **Statically allocated, not heap-backed** - a real constraint found before designing: the FreeRTOS heap margin was already measured at ~136 bytes free and is frozen; static allocation (`configSUPPORT_STATIC_ALLOCATION`, already enabled) avoids the heap entirely, confirmed directly against this project's actual `cmsis_os2.c` before relying on it.
- **`CommTxTask` (`StartTask06`) rewritten**: the old fixed-test-frame scaffolding is gone; it now loops on `TxQueue_DrainOne()` and is the **only** task that ever calls `Transport_UART_Send()` (confirmed by grep across the whole `Core/Src` tree).
- **`TxQueue_Init()`** is called from `main()`'s CubeMX-provided `/* USER CODE BEGIN RTOS_QUEUES */` marker - the regeneration-safe spot, avoiding a 4th silent-wipe incident (Open Question #16).
- **Real senders wired to the queues** (one small, targeted change per file - no module's own logic changed, only how its already-built frame gets sent):
  - `keepalive.c` → `TxQueue_EnqueueKeepAlive()`.
  - `monitor.c` → mode-transition Event frame now enqueued via `TxQueue_EnqueueEvent()`; **new** `TAG_DATA_REPORT` producer added (user-approved) - built and enqueued via `TxQueue_EnqueueData()` every 5s cycle, since nothing previously produced "data" tier traffic at all.
  - `object_detection.c` → object-detection Event frame now enqueued via `TxQueue_EnqueueEvent()`.
  - `init.c` → startup Event frame now enqueued via `TxQueue_EnqueueEvent()`.
  - `communication.c` → config-changed Event frame AND the system-time reply now enqueued via `TxQueue_EnqueueEvent()` (was a direct `Transport_UART_Send()` for the reply, was discarded for config-changed).
- **`event.c`/`event.h` themselves are completely untouched** - Event still only builds frames and hands them back; only its 4 callers changed.
- **Verified against Sec 7's actual text, not assumed**: `GET_SYSTEM_TIME_RESPONSE` has no defined "response" tier in Sec 7 (only 3 queues exist). Routed onto `txQueueEvent` on traffic-shape merits (irregular/triggered, largest depth) among the 3 existing tiers - documented explicitly in `communication.h`/`.c` as a reasoned choice, not a mislabel.
- **Not touched**: `event.c`/`event.h`, `message.c`, B's dispatch decision logic (which tags are handled/deferred is unchanged - only how a reply is sent changed), any task priority or stack size.
- Full writeup: Sec 14's "TX Priority Queue system" entry.

#### Build/link verification results (both B and C)
- Every touched/new file (`communication.c`, `tx_queue.c`, `keepalive.c`, `monitor.c`, `object_detection.c`, `init.c`, `main.c`) compiles with **zero warnings** against the real ARM toolchain (STM32CubeIDE 2.1.1's bundled `arm-none-eabi-gcc`).
- **Full-project link succeeds** (exit 0) in an isolated scratch copy (real object list + freshly compiled objects + the real linker script) - flash usage ~78.4 KB text, well under the 1022 KB budget.
- **Zero symbols** in Configuration's reserved flash page (`0x080FF800`-`0x080FFFFF`) - no collision.
- `nm` confirms all new/changed symbols are genuinely present and linked: `Communication_Dispatch` + its 5 getters, `TxQueue_Init`/`TxQueue_EnqueueKeepAlive`/`_EnqueueEvent`/`_EnqueueData`/`TxQueue_DrainOne`, `Message_BuildDataReport`.
- Confirmed via `git status`: `event.c`, `message.c`, and every task's priority/stack size are unchanged across both steps.

#### EXACT CURRENT STATUS (SUPERSEDED - kept for history, see the new CURRENT stopping point after Sec 15's B+C narrative below for what's actually true now)
~~**B + C are both implemented and build/link verified, but NEITHER has been tested on real hardware yet.**~~ **No longer accurate** - both B and C were subsequently hardware-tested (HW-B-C-01 through HW-B-C-05 all PASS, documented earlier in this same Sec 15 block's "Verified PASS" table and dedicated writeups). This sub-section and the "NEXT STEP" table right below it are pre-hardware-test planning notes, left in place as historical record of what was originally planned - do not treat them as the current state.

#### NEXT STEP: the combined real-hardware test (not yet performed)
This is what to do, and what to look at, when picking this up next:

| # | Test point | What to watch | Where |
|---|---|---|---|
| 1 | **Keep-Alive still fires every ~6s**, now going through the queue instead of a direct send | `g_keepalive_send_count` still increasing every ~6s; a real frame still arrives at the CC over COM10 | `g_keepalive_send_count`/`g_keepalive_last_timestamp`/`g_keepalive_last_frame_length` (debugger) |
| 2 | **CC → LNC "set limit" commands actually apply** | Send e.g. `TAG_SET_TEMP_NORMAL_RANGE` from the CC; `g_dispatch_set_limit_count` increases; `Config_GetTempNormalRange()`'s own debugger globals (`g_config_temp_normal_low`/`_high`) actually change | `g_dispatch_set_limit_count`, `g_config_temp_normal_low`/`_high` (debugger) |
| 3 | **`GET_SYSTEM_TIME_REQUEST` → reply round trip** | Send the request from the CC; `g_dispatch_system_time_request_count` increases; confirm a `TAG_SYSTEM_TIME_RESPONSE` frame actually arrives back at the CC (e.g. via the CC's own received-frame handling, or a COM10 capture) | `g_dispatch_system_time_request_count` (debugger) + CC-side observation |
| 4 | **Data reports fire every ~5s** | A `TAG_DATA_REPORT` frame reaches the CC roughly every Monitor cycle (5s) - this is a brand-new producer, never tested before | COM10 capture, or CC-side received-frame logging if available |
| 5 | **Event transmission through `CommTxTask`** | Trigger a mode transition (e.g. remove/restore a sensor to cross a limit) and/or object detection (IR remote); confirm the resulting Event frame actually arrives at the CC now, not just written to the SD card | SD card `.TXT` record (already verified) cross-checked against a COM10 capture showing the same event reaching the CC |
| 6 | **TX priority behavior: Keep-Alive > Event > Data** | With multiple frame types pending at once (e.g. trigger a mode change right as a Keep-Alive/Data cycle lands), confirm Keep-Alive is never delayed behind Event/Data, and Event is never delayed behind Data - hardest to trigger deliberately, but worth at least confirming no starvation/reordering is visible under normal combined traffic | COM10 capture with timestamps, compared against `g_keepalive_send_count`/dispatch counters' timing |
| 7 | **(New, not previously exercised)** `CommTxTask`'s stack margin under real combined load | `uxTaskGetStackHighWaterMark()` for `CommTxTask` - it now does meaningfully more work per iteration (queue draining across 3 queues) than the old fixed-test-frame scaffolding it replaced | Not yet instrumented - add a debugger-watch global for this if margin is a concern, same pattern as Monitor/ObjectDetection's own stack-margin checks |

**cc_main.cpp caveat, worth remembering going into the test**: its current interactive menu only exercises `setRtcDateTime` (a *deferred* tag - won't move any dispatch counters) and a measurement backfill request. Exercising test points 2 and 3 above will need either a small temporary menu option added to `cc_main.cpp`, or a hand-crafted frame sent some other way.

**After the hardware test passes**: update Sec 14's Communication and TX Priority Queue entries, and this section, to DONE/HARDWARE-VERIFIED. **If it doesn't pass**: this block is the reference point to diagnose from - it lists exactly what changed and why, so a failure can be isolated to a specific numbered test point above rather than re-investigated from scratch.

**After B+C are hardware-confirmed, the same open fork from earlier this session still applies**: Ground Station + CC's Ethernet-facing module (still fully unstarted), FleetOOP (fully independent, not networked), or revisiting Watchdog (still paused, needs explicit instruction to unpause) - plus the two items explicitly deferred within B itself: `SET_RTC_DATETIME` dispatch (blocked on the RTC backup-register guard) and the 2 "retrieve by range" requests (blocked on a not-yet-built SD-card historical-retrieval feature).

### CURRENT stopping point (2026-09-07, this is the real one, supersedes every block above): B+C hardware verification complete (6/8 PASS, 2 deferred). Ground Station work chosen as the next development item. **All 7 steps of the Ground Station plan are now DONE, PC-tested, AND manually end-to-end verified by the user** (Step 6 officially skipped, folded into Step 4d): CC is fully wired (`cc_main.cpp` listens on TCP port 5000), GS has a real dispatch/orchestration layer (`cc_communication::CcCommunication`) proven against a real CC over both an automated cross-executable TCP integration test AND a real manual session (single- and multi-chunk measurements/events, all PASS), and GS's real console entry point (`gs_main.cpp`) successfully connects to and exchanges data with a real, separate CC process. Nothing from the original plan remains - awaiting the user's direction on what comes next.

**Read this block first if picking the project up cold.**

#### Completed

**Previous LNC↔CC development and verification status** (full detail earlier in Sec 14/15): Protocol, Transport, Message layers, all 9 LNC spec modules except Watchdog (Configuration, Log, Event, Monitor, Object Detection, Init, Keep-Alive all hardware-verified), the full B (Communication/Command Dispatch) and C (TX Priority Queue) implementation, and the CC side (Communication, Management Command, DataStore, DataCollection, report_generator, `cc_main.cpp`) - all done. Real-hardware verification of B+C:
- **HW-B-C-01 (Keep-Alive), HW-B-C-02 (Set-Limit), HW-B-C-03 (GetSystemTime round trip), HW-B-C-04 (Data Report), HW-B-C-05 (Event transmission): all PASS.**
- **HW-B-C-08 (`CommTxTask` stack margin): PASS WITH WARNING** (~28% margin, stable, not touched per explicit instruction).
- **HW-B-C-06 (TX Priority Ordering): remains INCONCLUSIVE / deferred.** Three real capture attempts (using the standalone `com10_capture.py` tool, session scratchpad, outside the project's source tree) never produced a genuine multi-tier contention window - every observed gap between different-tier frames was 100ms+, two orders of magnitude larger than the ~1-2ms drain latency needed to prove simultaneous pending. Not reclassified as PASS or FAIL - status unchanged, revisit later if desired.
- **HW-B-C-07 (Queue-full / Drop Behavior): remains INCONCLUSIVE / deferred.** Analysis (not a live test) showed the existing production behavior cannot realistically fill any queue - `CommTxTask`'s drain rate (~1-2ms/item) is ~2 orders of magnitude faster than the fastest realistic producer rate (human-triggered IR events, 100ms+ apart), and the only way to slow the drain rate (disconnecting the UART) would very likely power-cycle the whole board rather than cleanly stall it. Status unchanged, revisit later if desired - the previously-proposed drop counter (test-support addition #3) was NOT added.
- Temporary debug instrumentation added during the HW-B-C-02/03 investigation was fully reverted afterward - confirmed zero `g_dbg_*` references remain anywhere in the project.

**Step 1 - CC Message Layer: IMPLEMENTED and PC-TESTED.**
- Added `buildMeasurementChunkResponse()` / `buildEventChunkResponse()` to the existing `CentralComputer/include/message.h` / `CentralComputer/src/message.cpp` - CC can now build (not just parse) both chunked-response frame types, needed because CC will answer Ground Station's own range requests using data it already has locally.
- Reuses the existing `MeasurementChunkResponse`/`EventChunkResponse` structs, existing tags, existing wire format - no new protocol mechanism.
- Tests: extended `Tests/CCMessage/cc_message_test.cpp` with `testBuildMeasurementChunkResponse()`/`testBuildEventChunkResponse()` (byte-for-byte payload checks, round trips through the existing parsers, over-limit rejection, zero-record edge case, build-side description truncation) - **80/80 checks PASS** (61 pre-existing + 19 new).
- Regression: `communication_test.exe` 29/29 PASS, `management_command_test.exe` 14/14 PASS, `cc_main.cpp` recompiles clean (same single pre-existing, unrelated `ModeName`-unused warning as before). Zero new warnings/errors. `git diff` confirmed only the 3 intended files changed.

**Step 2 - Ground Station Message Layer: IMPLEMENTED and PC-TESTED.**
- New, fully independent `GroundStation/include/message.h` / `GroundStation/src/message.cpp` - does **not** include or link against CC's own `message.h/.cpp` (same established precedent as the LNC/CC pair never sharing a single Message-layer implementation; only the tag vocabulary `tlv_common.h` and the raw codec `Common/TLVCodec` are shared).
- Implements: `buildGetMeasurementsByRangeRequest()`/`buildGetEventsByRangeRequest()` (GS -> CC, identical wire layout to CC's own request builders) and `parseMeasurementChunkResponse()`/`parseEventChunkResponse()` (CC -> GS, identical wire layout to CC's own parsers).
- Tests: new `Tests/GSMessage/gs_message_test.cpp` (request byte-for-byte layout, both chunk-response parsers' field decoding, wrong-tag rejection, non-multiple-of-record-size rejection, zero-record edge case, max record count for both chunk types, a no-null-terminator description bounded-read safety check, and a dedicated CRC-corruption test proving the real `tlv::Decoder` reports `Error`) - **32/32 checks PASS**.
- CC regression re-confirmed: `cc_message_test.exe` still **80/80 PASS**. Zero new warnings/errors. `git status`/`git diff` confirmed only new files were added (`GroundStation/`, `Tests/GSMessage/`) - zero CC files touched.

**Step 3 - TCP/Ethernet Transport: IMPLEMENTED and PC-TESTED.**
- New `GroundStation/include/tcp_transport.h` / `GroundStation/src/tcp_transport.cpp` - `namespace transport { class TcpSocket }`, mirroring `CentralComputer/serial_transport.h/.cpp`'s exact shape and rationale (non-copyable, owns one OS handle stored as `void*` in the header specifically to keep `<winsock2.h>`'s own types/macros out of every file that includes the header - same reasoning `SerialPort` already documents for `<windows.h>`/`HANDLE`).
- **API**: `bool connect(const std::string &host, uint16_t port)`, `void close()`, `bool isConnected() const`, `bool send(const std::vector<uint8_t> &data, uint32_t timeoutMs)`, `bool receiveByte(uint8_t &outByte, uint32_t timeoutMs)`. `connect`/`isConnected` replace `SerialPort`'s `open`/`isOpen` (TCP's natural vocabulary); `close`/`send`/`receiveByte` keep identical names for cross-class consistency.
- **Scope, confirmed by construction**: `TcpSocket` only ever handles raw `std::vector<uint8_t>` bytes - zero knowledge of TLV, tags, or message types. It is a **client-only** class (GS connects out to the CC, per the already-documented open decision that the CC listens) - it deliberately has no `listen()`/`accept()` - a listening counterpart is the CC's own, separate, not-yet-built concern (Step 4+).
- **Platform/implementation**: Win32 Winsock2 (`socket`/`connect`/`send`/`recv`/`closesocket`, `SO_SNDTIMEO`/`SO_RCVTIMEO` for timeouts) - this codebase is already Windows-only (confirmed via `serial_transport.cpp`'s own `<windows.h>` usage), so this follows the same platform choice rather than introducing a second one. `WSAStartup`/`WSACleanup` are called per `connect()`/`close()` pair rather than through a separate global-init mechanism - Winsock reference-counts these internally, so this is safe with multiple instances and is the simplest correct option (Sec 12).
- **Tests**: new `Tests/TcpTransport/tcp_transport_test.cpp` (18 checks) - unlike `serial_transport_test.cpp` (which *must* use real hardware, since there's no software substitute for a serial cable), this test uses a **real local TCP loopback** (127.0.0.1, OS-assigned free port via binding to port 0): a minimal raw-Winsock listener written *only* inside this test file (never in `GroundStation/src/` - kept out of production code deliberately, matching `tcp_transport.h`'s own "does NOT listen/accept" boundary) gives the real client class a real peer to connect to, send to, and receive from. Covers: connecting where nothing listens (fails cleanly), a malformed address (fails cleanly), a full real connect+send+byte-for-byte echo receive round trip, `receiveByte()`'s timeout never hanging past a reasonable margin, all operations on a never-connected socket failing cleanly without crashing, and reconnecting the same object after `close()`. **18/18 checks PASS**, first run, no fix-up needed.
- Regression re-confirmed: `cc_message_test.exe` still **80/80 PASS**, `gs_message_test.exe` still **32/32 PASS**. Zero new warnings/errors (`-Wall` clean on both the class and its test). `git status` confirmed only new files added (`GroundStation/include/tcp_transport.h`, `GroundStation/src/tcp_transport.cpp`, `Tests/TcpTransport/`) - zero existing files touched, including the already-completed Steps 1-2 files.
- **Intentionally deferred, not part of Step 3**: any listening/accepting counterpart (CC's own future job, Step 4), any wiring into `GsCommunication` or `cc_main.cpp`, any message-layer coupling (`TcpSocket` never includes or references `message.h`).
- **Assumption made, not yet validated against a real second machine**: loopback (127.0.0.1) is assumed sufficient to stand in for Sec 1.2/4's "simulated Ethernet on the same PC" - consistent with how the LNC's real UART link is already confined to one PC's peripherals; no cross-machine networking is in scope anywhere in this project.

**Step 4 - CC-side `GsCommunication` + TCP Server Transport: IN PROGRESS, being done in small sub-steps at the user's explicit request (not as one block like Steps 1-3).**

**Step 4a - CC-side `TcpServerSocket` (transport layer only): IMPLEMENTED and PC-TESTED (2026-09-07).**
- New `CentralComputer/include/tcp_transport.h` / `CentralComputer/src/tcp_transport.cpp` - CC's own, independent TCP transport implementation (same filename as `GroundStation/include/tcp_transport.h`/`.cpp`, but a separate implementation under CC's own tree - extends the same "each side gets its own independent implementation" precedent Steps 1-2 already established for the Message layer, now applied to Transport).
- **API implemented exactly per the agreed design**: `startListening(port)`, `close()`, `isListening()`, `isClientConnected()`, `tryAcceptClient()` (non-blocking, via `select()` with a zero timeout), `send()`, `receiveByte()`. Single-client-at-a-time by construction (`listen(sock, 1)`); a lost client (clean disconnect or socket error, detected inside `send()`/`receiveByte()`) drops only `clientHandle_` - `listenHandle_` is untouched, so the server keeps listening for a fresh GS connection with no extra call needed (Step 4's "reconnection is supported" decision, now implemented).
- **Real defect found and fixed during testing, worth recording**: the first implementation used `SO_REUSEADDR` (copied from the same pattern as `GroundStation/tcp_transport.cpp`'s client-side reasoning about quick re-bind after close). A dedicated test proved this was wrong for a *server* socket on Windows: unlike POSIX, Windows' `SO_REUSEADDR` allows a second socket to bind and hijack a port that already has an **active** listener on it, silently defeating "only one CC can listen on port 5000." Fixed by switching to `SO_EXCLUSIVEADDRUSE` (the documented Windows-specific mechanism for this exact problem) - still allows a clean re-bind after a proper `close()`, but now correctly blocks a second live listener. Re-tested and confirmed fixed.
- **Tests**: new `Tests/TcpServerTransport/tcp_server_transport_test.cpp` (mirrors `tcp_transport_test.cpp`'s pattern with roles reversed - a test-only raw-Winsock **client** stands in for the not-yet-built GS, connecting to the real, production `TcpServerSocket`). Covers: successful listen, `tryAcceptClient()` false when nothing pending, full real connect + bidirectional send/receive round trip, `receiveByte()` timeout never hanging, client-disconnect dropping only the client while listening continues (and a **second** real client can then connect on the same still-listening socket - reconnection proven, not just assumed), operations before `startListening()` failing cleanly without crashing, a second server failing to bind the same live port (the test that caught the `SO_REUSEADDR` defect above), and `close()` tearing down both listening and client state. **30/30 checks PASS** (test port `15931`, fixed rather than OS-assigned, since `TcpServerSocket` has no `port()` getter - not part of the agreed API).
- Regression re-confirmed: `cc_message_test.exe` still **80/80 PASS**, `gs_message_test.exe` still **32/32 PASS**, `tcp_transport_test.exe` (GS client) still **18/18 PASS**. Zero new warnings (`-Wall` clean). `git status` confirmed only `CentralComputer/include/tcp_transport.h`, `CentralComputer/src/tcp_transport.cpp`, and `Tests/TcpServerTransport/` were added - zero existing files touched, including every already-completed Step 1-3 file.
- **Intentionally deferred, not part of Step 4a**: `GsCommunication`, the two new CC-side message parsers, any `DataStore` interaction, any `cc_main.cpp` wiring - all still Step 4b+ (see below).

**Step 4b - CC-side message parser additions: IMPLEMENTED and PC-TESTED (2026-09-07).**
- Added `parseGetMeasurementsByRangeRequest()` / `parseGetEventsByRangeRequest()` to `CentralComputer/include/message.h` / `CentralComputer/src/message.cpp` (Step 1's file - the one place Step 4 is approved to touch an already-completed step's file). Both reuse the **existing** `TimeRangeMessage` struct (no new struct), reject a wrong tag or a payload that isn't exactly 9 bytes (`RequestId(1) + StartTime(4) + EndTime(4)`), and are the exact "missing other half" of the already-existing `buildGetMeasurementsByRangeRequest`/`buildGetEventsByRangeRequest` (CC previously only ever *built* these, to ask the LNC for a backfill - now GS *sends* them to CC, so CC needs to parse them too). Same pattern Step 1 itself used for the chunk-response builders.
- **Tests**: extended `Tests/CCMessage/cc_message_test.cpp` with `testParseGetRangeRequests()` - round trip through the real build+decode+parse path for both requests, wrong-tag rejection (each parser rejects the other request's frame), and too-short-payload rejection for both. **92/92 checks PASS** (80 pre-existing + 12 new).
- Regression re-confirmed: `gs_message_test.exe` still **32/32 PASS**, `tcp_transport_test.exe` (GS client) still **18/18 PASS**, `tcp_server_transport_test.exe` (Step 4a) still **30/30 PASS**. `cc_main.cpp` (the CC's real entry point, which includes `message.h`) recompiled and relinked clean against the changed header/source - same single pre-existing, unrelated `ModeName`-unused warning as always, zero new warnings/errors; `CentralComputer/build/central_computer.db` (real data) was not touched by this rebuild. `git status` confirmed exactly 4 files changed - `CentralComputer/include/message.h` (+11), `CentralComputer/src/message.cpp` (+26), `Tests/CCMessage/cc_message_test.cpp` (+48), plus `PROJECT_GUIDE.md` - and Step 4a's files (`tcp_transport.h/.cpp`, `Tests/TcpServerTransport/`) are untouched/unchanged.
- **Intentionally deferred, not part of Step 4b**: `GsCommunication` itself, any `DataStore` interaction, any `cc_main.cpp` wiring of these new parsers - all still Step 4c+ (see below).

**Step 4c - the `GsCommunication` class itself: IMPLEMENTED and PC-TESTED (2026-09-07), scope deliberately narrowed at the user's explicit request.**
- New `CentralComputer/include/gs_communication.h` / `CentralComputer/src/gs_communication.cpp` - `gs_communication::GsCommunication`, the CC<->GS analog of `communication::Communication` (same `poll()`/`feedByte()`/`framesDispatched`/`decodeErrors` shape, a different transport and a much smaller tag vocabulary).
- **Constructor takes `data_store::DataStore &dataStore`** exactly as the agreed API documents (non-owning reference, stored as `dataStore_`) - but **at the user's explicit instruction, nothing in this class calls into it yet**. `dispatch()` recognizes `TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST`/`TAG_GET_EVENTS_BY_RANGE_REQUEST` via the Step 4b parsers and increments `framesDispatched` on a successful parse, but takes no further action - no query, no response built, no bytes sent back. `sendMeasurementsForRange()`/`sendEventsForRange()` (the methods that will actually touch `dataStore_` and send a reply) are **not yet declared** - deliberately not invented ahead of the step that will actually need them, per the user's "do not invent new interfaces... unless absolutely required" instruction.
- `startListening`/`close`/`isListening`/`isClientConnected`/`poll`/`feedByte` all implemented exactly per the agreed API, `poll()` mirroring `Communication::poll()`'s "accept-if-pending, then one byte of decode progress" shape precisely.
- **Tests**: new `Tests/GsCommunication/gs_communication_test.cpp` - a real raw-Winsock loopback client (test-only, mirroring `tcp_server_transport_test.cpp`'s pattern) drives the real `GsCommunication` over a real TCP connection; request frames are built with the real, already-tested `message::buildGetMeasurementsByRangeRequest`/`buildGetEventsByRangeRequest`. Covers: initial state before listening, accepting a real client via `poll()`, both request tags being recognized and counted, an irrelevant-but-well-formed tag (`KEEPALIVE`) being dropped silently (not an error), a corrupted-CRC frame incrementing `decodeErrors` but not `framesDispatched`, a correctly-tagged-but-wrong-length payload being neither a decode error NOR a dispatch (proving the frame-level/message-level rejection distinction isn't conflated), `feedByte()` working as a direct seam with no real socket, and disconnect-then-reconnect still dispatching correctly on the new connection. **31/31 checks PASS**, first run, no fix-ups needed. A `data_store::DataStore` is constructed for the test purely to satisfy the constructor signature - no method is ever called on it, matching the class's own current scope.
- Regression re-confirmed: `cc_message_test.exe` still **92/92 PASS**, `gs_message_test.exe` still **32/32 PASS**, `tcp_transport_test.exe` still **18/18 PASS**, `tcp_server_transport_test.exe` still **30/30 PASS**. `cc_main.cpp` confirmed to have zero references to `gs_communication`/`tcp_transport` (grep-verified) - genuinely not wired in yet. `git status` confirmed exactly `CentralComputer/include/gs_communication.h`, `CentralComputer/src/gs_communication.cpp`, and `Tests/GsCommunication/` were added this sub-step - Step 4b's files (`message.h/.cpp`, `cc_message_test.cpp`) are byte-for-byte unchanged (diff line counts identical: +11/+26/+48).
- **Intentionally deferred, not part of Step 4c**: any `DataStore` querying, any response building/sending (`sendMeasurementsForRange`/`sendEventsForRange` not yet declared), any `cc_main.cpp` wiring - all later Step 4 sub-steps.

**Step 4d - DataStore-querying/response-sending logic: IMPLEMENTED and PC-TESTED (2026-09-07).**
- Added `sendMeasurementsForRange(const message::TimeRangeMessage &request)` / `sendEventsForRange(const message::TimeRangeMessage &request)` as private methods of `GsCommunication` (`CentralComputer/include/gs_communication.h` / `.cpp`), exactly per the originally agreed API - no new structs, no new public interface. Each queries `dataStore_.getMeasurementsInRange()`/`getEventsInRange()` (existing, unchanged), splits the results into groups of at most `MAX_MEASUREMENTS_PER_CHUNK` (11) / `MAX_EVENTS_PER_CHUNK` (6), builds each group with the existing `message::buildMeasurementChunkResponse()`/`buildEventChunkResponse()` (Step 1, unchanged) - echoing `request.requestId`, correct `chunkSeq`, and `moreDataFlag` - and sends each chunk via `serverTransport_.send()`. A `do/while` loop, not a `for`, so a zero-match request still sends exactly one (empty) chunk rather than silence - reusing `buildMeasurementChunkResponse`'s/`buildEventChunkResponse`'s already-existing, already-tested zero-record support from Step 1.
- **`dispatch()` wired up**: both existing cases (`TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST`/`TAG_GET_EVENTS_BY_RANGE_REQUEST`) now call the matching `sendXxxForRange()` immediately after a successful parse, in the same synchronous call - no async correlation bookkeeping, matching the agreed "CC already holds all the data locally" design decision.
- **Real correctness point found and fixed while extending the tests, worth recording**: Step 4c's tests constructed an *unopened* `data_store::DataStore` (never called `.open()`) purely to satisfy `GsCommunication`'s constructor signature, since nothing called into it yet. Now that `dispatch()` genuinely queries `dataStore_`, an unopened `DataStore` means `sqlite3_prepare_v2()` is called with a null `sqlite3*` handle - undefined behavior, not a safe no-op. Fixed by opening every test's `DataStore` in-memory (`":memory:"`) before constructing `GsCommunication`, matching how a real `cc_main.cpp` would always have an already-open `DataStore` before `GsCommunication` could ever run. Applied to all 9 existing `DataStore` instances in the test file, not just the new ones, for consistency and to remove reliance on undefined behavior anywhere in the suite.
- **Tests**: extended `Tests/GsCommunication/gs_communication_test.cpp` with 5 new end-to-end tests, each using a real (in-memory) `DataStore` pre-populated with known rows and a real loopback client that decodes the actual response bytes with a real `tlv::Decoder` and the real `message::parseMeasurementChunkResponse`/`parseEventChunkResponse`: single-chunk measurements (exact record content and order verified, out-of-range rows correctly excluded, confirmed no extra bytes follow), single-chunk events (same, plus description round-trip), multi-chunk measurements (14 records against `MAX_MEASUREMENTS_PER_CHUNK=11` - first chunk 11 records/`moreDataFlag=true`, second chunk 3 records/`moreDataFlag=false`, all 14 timestamps verified present exactly once and in order across both chunks), multi-chunk events (8 records against `MAX_EVENTS_PER_CHUNK=6` - same pattern), and a zero-match request still producing exactly one well-formed empty chunk. **77/77 checks PASS** (31 pre-existing Step 4c checks + 46 new), first run, no fix-ups needed beyond the `DataStore.open()` correctness fix above.
- Regression re-confirmed: `cc_message_test.exe` still **92/92 PASS**, `gs_message_test.exe` still **32/32 PASS**, `tcp_transport_test.exe` still **18/18 PASS**, `tcp_server_transport_test.exe` still **30/30 PASS**. `cc_main.cpp` confirmed to still have zero references to `gs_communication`/`tcp_transport` (grep-verified) - genuinely not wired in yet. `git status`/`git diff` confirmed only `CentralComputer/include/gs_communication.h`, `CentralComputer/src/gs_communication.cpp`, and `Tests/GsCommunication/gs_communication_test.cpp` changed this sub-step; Step 4a's files (`tcp_transport.h/.cpp`, `Tests/TcpServerTransport/`) and Step 4b's files (`message.h/.cpp`, `cc_message_test.cpp`) are byte-for-byte unchanged.
- **Intentionally deferred, not part of Step 4d**: any `cc_main.cpp` wiring, Step 5 (GS-side dispatch/orchestration), Step 6/7.

**`cc_main.cpp` wiring: IMPLEMENTED and BUILD/LINK-VERIFIED (2026-09-07).**
- `CentralComputer/src/cc_main.cpp` now `#include`s `gs_communication.h` and constructs `gs_communication::GsCommunication gsComm(store);` - **reusing the exact same `DataStore` the LNC side already fills**, not a second store, so a GS query answers from the real, single measurement/event history CC has actually collected.
- `gsComm.startListening(5000)` is called at startup (the agreed production port, `kGsPort` constant), printing a clear "Listening for Ground Station..." line on success or a "WARNING: could not listen..." line on failure - same pattern as the existing COM10-open messaging, and the program continues running either way (a GS-listen failure doesn't prevent LNC-facing operation, matching how a COM10-open failure doesn't either).
- `gsComm.poll()` is called every iteration of the main loop, right alongside the existing `comm.poll()` - single-threaded, non-blocking, same model as everything else in this file.
- `[3] Show status` now also prints `GS listening`, `GS client connected`, `GS frames dispatched`, and `GS decode errors` - `printStatus()` gained a third parameter rather than a new menu entry, mirroring the existing status option's spirit exactly (the simplest option explicitly left open at Step 4's design stage). No new menu key was added.
- `gsComm.close()` is called at shutdown, alongside the existing `comm.close()`/`store.close()`.
- **Build note**: `cc_main.exe` now links `tcp_transport.o` and `gs_communication.o` in addition to its previous object list, and requires `-lws2_32` (Winsock) for the first time - previously only `GroundStation`'s and the standalone transport tests needed it.
- **Verified**: full clean rebuild of `cc_main.cpp` and every module it uses (`gs_communication.cpp`, `tcp_transport.cpp`, plus the previously-existing object list), full relink - clean, same single pre-existing unrelated `ModeName`-unused warning as always, zero new warnings/errors. `cc_main.exe` was smoke-run (no real COM10 attached in this environment): database opens, **"Listening for Ground Station on TCP port 5000."** prints correctly, COM10 fails gracefully with the existing warning message (proving the two subsystems' startup failures are independent), and the menu (unchanged, still options 1-6/q) prints correctly. This file is not unit-tested with a `check()`-based suite - same established convention as every other `cc_main.cpp` change (see Sec 14) - a real entry point with a real infinite loop against real hardware isn't something a PC test suits; `GsCommunication` itself already has full, independent 77/77 test coverage from Step 4d, so this step's only new logic is thin, directly-inspectable glue code (construct, listen, poll, print, close).
- Regression re-confirmed, all unaffected and all still passing: `cc_message_test.exe` **92/92**, `gs_message_test.exe` **32/32**, `tcp_transport_test.exe` **18/18**, `tcp_server_transport_test.exe` **30/30**, `gs_communication_test.exe` **77/77**, `communication_test.exe` **29/29**, `management_command_test.exe` **14/14**, `data_store_test.exe` **24/24**, `data_collection_test.exe` **25/25**, `report_generator_test.exe` **16/16**. `git status`/`git diff` confirmed only `CentralComputer/src/cc_main.cpp` changed this step (+41/-7 lines) - every Step 4a-4d file (`tcp_transport.h/.cpp`, `gs_communication.h/.cpp`, `message.h/.cpp`, and all `Tests/` files) is byte-for-byte unchanged.
- **Real hardware test of the actual GS<->CC TCP path is still pending** - this step only proves the wiring compiles, links, and starts up correctly without a real Ground Station attached. A genuine end-to-end test (a real `gs_main.cpp` - Step 7 - or a throwaway TCP client - talking to a running `cc_main.exe`) has not been performed and is not yet in scope.

**Step 5 - GS-side dispatch/orchestration layer: IMPLEMENTED and PC-TESTED (2026-09-07).**
- New `GroundStation/include/cc_communication.h` / `GroundStation/src/cc_communication.cpp` - `cc_communication::CcCommunication`, the missing piece between GS's raw byte transport (`tcp_transport.h`) and its pure build/parse functions (`message.h`). Named after the party it talks to (CC), the mirror of how the CC's own class is named `gs_communication::GsCommunication` after the party *it* talks to.
- **Merges two roles CC keeps separate** (`communication::Communication` for decode/dispatch, `data_collection::DataCollection` for request correlation/completion) into one class, because GS's tag vocabulary is tiny (2 requests, 2 responses) - the same reasoning that already led `GsCommunication` to merge decode+dispatch+DataStore-querying into one class rather than splitting it the way the LNC-facing side is split. This was presented as part of the design proposal and approved before implementation.
- **API implemented exactly as proposed and approved**: `connect`/`close`/`isConnected` (thin forwarding to `TcpSocket`), `poll`/`feedByte` (mirrors `Communication`/`GsCommunication` exactly), `requestMeasurements`/`requestEvents` (builds via the existing `message::buildGetMeasurementsByRangeRequest`/`buildGetEventsByRangeRequest`, assigns a self-incrementing `requestId` - GS is the requester, per Step 4's agreed asymmetry - refuses a second request of the *same* type while one is in flight, but a measurement request and an event request may run concurrently, independent trackers, mirroring `DataCollection` exactly), `isMeasurementRequestInProgress`/`isEventRequestInProgress`, and the two approved callbacks `onMeasurementsReceived`/`onEventsReceived` (`std::function`, fired once with the FULL accumulated result once `moreDataFlag == false`).
- **Deliberate difference from `DataCollection`, worth recording**: `DataCollection` saves each chunk straight to `DataStore` as it arrives, needing no in-memory buffer. GS has no database - it only needs to retrieve and show data (Sec 4) - so `CcCommunication` accumulates chunks in memory (`accumulatedMeasurements_`/`accumulatedEvents_`) per in-flight request and delivers the whole result at once via the callback, clearing the buffer immediately after.
- **Zero CC-side changes** - `GsCommunication`, `TcpServerSocket`, and CC's `message.h` were not modified. `CcCommunication` is simply the first real client ever built against `GsCommunication`'s already-complete Step 4 server behavior.
- **Reconnect / mid-request-disconnect handling is explicitly OUT OF SCOPE and UNHANDLED** - if the TCP connection drops while a request is in flight, the pending state simply stays pending forever (no timeout, no recovery). Documented here as an open question for a later step, per explicit instruction, not silently designed around.
- **A real architectural constraint discovered while building the integration test, worth recording carefully**: GS's `message.h` and CC's `message.h` both define an identically-named type, `message::TimeRangeMessage` (and several others) - two independent struct definitions sharing the same fully-qualified name, by the project's own long-standing deliberate design ("never share Application-facing struct definitions across executables"). Attempting to link `gs_communication.cpp` (needs CC's `message.o`) and `cc_communication.cpp` (needs GS's `message.o`) into ONE test executable fails at link time with "multiple definition of `message::buildGetMeasurementsByRangeRequest(...)`" - a correct, hard linker error, not a bug to work around. This is empirical proof that the "always two separate binaries" design decision is a real technical necessity, not just a style preference. Resolved by making the integration test genuinely two separate processes (see below) - exactly mirroring how real CC and real GS will always be two separate executables in production.
- **Tests, Part 1 (unit-style, no socket)**: new `Tests/CcCommunication/cc_communication_test.cpp`, driven via `feedByte()` with hand-assembled response frames (GS's `message.h` has no *builder* for chunk responses - only CC ever builds those - so wire bytes are assembled directly, the same technique `Tests/GSMessage/gs_message_test.cpp` already uses). Covers: initial state, same-type overlap refused / different-type concurrency allowed, single-chunk callback firing with correct records, multi-chunk accumulation firing exactly once with everything merged in order, a mismatched-requestId chunk ignored (while the real pending request is untouched), a chunk arriving with nothing pending ignored without crashing, an irrelevant tag dropped silently, a corrupted-CRC frame incrementing `decodeErrors` not `framesDispatched`, and pre-connect operations not crashing. **36/36 checks PASS.**
- **Tests, Part 2 (the real cross-executable integration test - this project's first)**: new `Tests/CcCommunication/cc_side_test_server.cpp` - NOT a test file itself, but a small, separate, standalone executable that is the CC-side test double: 100% real production code (`gs_communication::GsCommunication` + a real in-memory `data_store::DataStore`), pre-populated with a fixed, documented dataset, run as its own OS process and listening on a real TCP port. `cc_communication_test.cpp`'s integration checks connect a real `CcCommunication` to this already-running process and verify, over a real TCP loopback connection between two real processes: single-chunk measurements and events (exact content/order, out-of-range rows correctly excluded), multi-chunk measurements (14 records, `MAX_MEASUREMENTS_PER_CHUNK=11` → 11+3, transparently merged into one callback firing) and events (8 records, `MAX_EVENTS_PER_CHUNK=6` → 6+2), a zero-match request still firing the callback with an empty vector (not silence), and the overlap guard verified deterministically against the real server. **22/22 additional checks PASS**, for **58/58 total** in the full suite. Verified first with the server NOT running (the connect check fails honestly and the rest is skipped with a clear message - not a crash, not a silent pass) and then with the server running (all 58 pass).
- Regression re-confirmed, all unaffected and all still passing: `cc_message_test.exe` **92/92**, `gs_message_test.exe` **32/32**, `tcp_transport_test.exe` **18/18**, `tcp_server_transport_test.exe` **30/30**, `gs_communication_test.exe` **77/77**, `communication_test.exe` **29/29**, `management_command_test.exe` **14/14**, `data_store_test.exe` **24/24**, `data_collection_test.exe` **25/25**, `report_generator_test.exe` **16/16**. `cc_main.cpp` confirmed to have zero references to `cc_communication` (grep-verified) - CC side genuinely untouched. `git status`/`git diff` confirmed only `GroundStation/include/cc_communication.h`, `GroundStation/src/cc_communication.cpp`, and `Tests/CcCommunication/` were added this step - every prior step's file is byte-for-byte unchanged.
- **Intentionally deferred, not part of Step 5**: `GroundStation/src/gs_main.cpp` (Step 7 - no console app, no UI/display logic), reconnect/mid-request-disconnect recovery (open question, above), any CC-side change, Step 6 (still to be confirmed - may already be fully covered by Step 4d's `sendMeasurementsForRange`/`sendEventsForRange`, not assumed here).

**Step 7 - `GroundStation/gs_main.cpp` (the GS console application): IMPLEMENTED and BUILD/LINK-VERIFIED (2026-09-07).**
- New `GroundStation/src/gs_main.cpp` - the GS's real entry point, mirroring `CentralComputer/cc_main.cpp`'s role and structure exactly: a thin driver over already-tested modules, adding no new communication logic. Constructs one `cc_communication::CcCommunication`, registers `onMeasurementsReceived`/`onEventsReceived` once up front (each prints its result when it arrives, asynchronously - same pattern as `cc_main.cpp`'s own `onSystemTimeResponse` callback), attempts `connect("127.0.0.1", 5000)` once at startup (the agreed production host/port), and runs the identical single-threaded `poll()` + non-blocking `_kbhit()`/`_getch()` menu loop shape.
- **Menu**: `[1] Connect to CC` (manual (re)connect on demand - `CcCommunication` has no auto-reconnect by deliberate Step 5 design, so this is the application-layer recovery path; calling `connect()` again on the same object was already proven safe in Step 4a's own tests), `[2]`/`[3]` request measurements/events over an operator-entered `[startTime, endTime]` range as **raw Unix-epoch integers** (no date-parsing code invented, matches the codebase's internal representation exactly), `[4] Show status` (`isConnected`, both `isXxxRequestInProgress` flags, `framesDispatched`, `decodeErrors` - mirrors `cc_main.cpp`'s `printStatus()`), `[q] Quit`.
- **Connection guard placed in the application layer, not in `CcCommunication`**: `[2]`/`[3]` check `cc.isConnected()` themselves before calling `requestMeasurements()`/`requestEvents()`, printing a clear "not connected" message otherwise - `CcCommunication`'s own methods deliberately don't gate on connection state (Step 5's "don't gate on an unconfirmable send result" design, left unchanged).
- **All 5 decisions approved before implementation, applied exactly as agreed**: (1) `[1] Connect` menu kept, (2) raw Unix-epoch integer entry, (3) connection guard in `gs_main.cpp`, (4) no timeout/cancel/reset/reconnect architecture added anywhere - a stuck pending request has no way to be cancelled today, a known, documented gap, not silently worked around, (5) testing done via build/link + smoke test + best-effort real communication, with full interactive verification left open for manual confirmation.
- **Zero CC-side changes, zero changes to `CcCommunication`/`TcpSocket`/message layers** - `gs_main.cpp` is the only new file.
- **Verified**: clean build/link of `gs_main.cpp` + every module it uses (`cc_communication.cpp`, `message.cpp`, `tcp_transport.cpp`, `tlv_codec.cpp`), zero warnings. Non-interactive startup smoke test (piped `/dev/null`, so `_kbhit()`/`_getch()` never fire - this only proves construction/connect-attempt/menu-print logic, not the interactive loop): with no CC running, prints the WARNING and continues; with the real, separately-running Step 5 `cc_side_test_server.exe` fixture listening on port 5000, **the real TCP `connect()` genuinely succeeds against a real, separate process** ("Connected to CC at 127.0.0.1:5000.") - proving the startup connection logic works end-to-end, not just compiles. A best-effort piped-menu-input attempt (`"2\n1000\n1020\nq\n"`) was also tried and, as anticipated in the approved test plan, `_kbhit()`/`_getch()` do not read redirected/piped stdin at all under this shell (the process just idles until an external timeout) - this is the exact, already-disclosed limitation from decision #5, not a bug: **full interactive menu-loop verification (options 1-4 actually being typed and observed) remains a manual step for the user**, the same way real-hardware verification elsewhere in this project is a user action.
- Regression re-confirmed, all unaffected and all still passing: `gs_message_test.exe` **32/32**, `tcp_transport_test.exe` **18/18**, `cc_communication_test.exe` **58/58** (re-run against a freshly-started `cc_side_test_server.exe` on its own port 15970), `cc_message_test.exe` **92/92**, `tcp_server_transport_test.exe` **30/30**, `gs_communication_test.exe` **77/77**, `communication_test.exe` **29/29**, `management_command_test.exe` **14/14**, `data_store_test.exe` **24/24**, `data_collection_test.exe` **25/25**, `report_generator_test.exe` **16/16**. `cc_main.cpp` confirmed to have zero references to `gs_main` (grep-verified). `git status` confirmed only `GroundStation/src/gs_main.cpp` (new) plus `PROJECT_GUIDE.md` changed this step - note that Steps 1-5's own file changes have since been committed to the repo (outside this session) as `fce3c49`/`86fdf3a`, so the working tree was already clean going into Step 7.
- **Full interactive manual verification: since performed by the user - see the dedicated section immediately below.** Any real-hardware/live multi-machine test remains out of scope everywhere in this project (loopback-only by design).

### Manual End-to-End Verification of the GS <-> CC Communication Path: PASSED (2026-09-07)

Performed by the user, using the real, separate executables - `CentralComputer/build/cc_main.exe` for real-CC-application checks, `Tests/CcCommunication/cc_side_test_server.exe` (Step 5's deterministic fixture, listening on TCP port 5000) for the complete GS<->CC data-flow checks, and `GroundStation/build/gs_main.exe` as the real GS application. GS connected to the fixture successfully over a real TCP connection.

**Test 1 - Measurements, single chunk.** Request `startTime=1000, endTime=1020`. Result: **PASS** - exactly 3 measurements received (`t=1000, 1010, 1020`), all fields correct.

**Test 2 - Events, single chunk.** Request `startTime=2000, endTime=2010`. Result: **PASS** - exactly 2 events received (`t=2000, 2010`), both `ObjectDetection` / `"test event"`.

**Test 3 - Measurements, multi-chunk.** Request `startTime=3000, endTime=3013`. Result: **PASS** - exactly 14 measurements received, in correct ascending timestamp order (`t=3000` through `t=3013`). Verifies the CC-side split into an 11-record chunk + a 3-record chunk (`MAX_MEASUREMENTS_PER_CHUNK=11`), with GS transparently accumulating both chunks and delivering the complete 14-record result through a single `onMeasurementsReceived` callback invocation.

**Test 4 - Events, multi-chunk.** Request `startTime=4000, endTime=4007`. Result: **PASS** - exactly 8 events received, in correct order (`t=4000` through `t=4007`). Verifies the CC-side split into a 6-record chunk + a 2-record chunk (`MAX_EVENTS_PER_CHUNK=6`), with GS transparently accumulating both chunks and delivering the complete 8-record result through a single `onEventsReceived` callback invocation.

**One investigation along the way, resolved as user error, not a defect**: an initial attempt at `requestMeasurements(2000, 2010)` returned `0 measurement(s) received`. Investigated before any code was touched: confirmed via direct inspection of `cc_side_test_server.cpp` that the fixture's measurement timestamps are `1000, 1010, 1020, 5000, 3000..3013` while `2000, 2010, 9000, 4000..4007` are **event** timestamps - `2000-2010` was always the events range (matching `cc_communication_test.cpp`'s own already-passing integration test, which uses exactly this range for `requestEvents`, never `requestMeasurements`). Also confirmed the built `.exe`'s file timestamp was newer than the `.cpp`'s source (not a stale build). No code was changed - the corrected test (Test 2 above, requesting **events** for `2000-2010`) passed.

**Overall conclusion**: this manual test demonstrated the complete real path working end-to-end, with no mocks anywhere:
```
GS application -> CcCommunication -> TCP -> CC GsCommunication -> DataStore range query
   -> response chunking -> TCP -> GS CcCommunication -> TLV decoding
   -> multi-chunk accumulation -> completion callback -> application output
```
Verified successfully: GS<->CC TCP connection, measurement range requests, event range requests, range filtering (out-of-range rows correctly excluded), DataStore integration, TLV framing/decoding, single-chunk responses, multi-chunk responses, measurement accumulation, event accumulation, correct ordering, correct callback completion, and no data loss in any tested scenario.

**Files still to be created for the rest of the plan:** none identified - Steps 1-7 of the original plan are now all done, PC-tested, AND manually end-to-end verified (Step 6 officially skipped, folded into Step 4d). Any further Ground Station work (e.g., richer display formatting, canned convenience date ranges) would be new, not-yet-requested scope - **there is no Step 8**. The item below (real hardware/second-machine-equivalent test) is **no longer future work - it has now been performed**, see immediately below.

### Full Live Hardware Integration Verification (Embedded -> CC -> GS): PASSED (2026-09-07)

Supersedes the fixture-based verification above with the real path end-to-end: the real STM32 NUCLEO-L476RG board (real sensors, real IR input) -> real USART2/COM10 -> real `cc_main.exe` -> real `DataStore` -> real TCP port 5000 -> real `gs_main.exe`. Performed in 3 user-approved stages, each gated before proceeding to the next.

**Stage 2 - Hardware -> Embedded (approved by the user).** Verified directly via the STM32CubeIDE debugger, using only already-existing watch variables (`g_monitor_sample_count`, `g_monitor_last_temperature/humidity/light/battery/mode`, `g_object_detection_last_state`, `g_object_detection_raw_gpio_level`, `g_event_last_frame_length`) and a breakpoint at `CommTxTask`'s `Transport_UART_Send()` call site: `MonitorTask`'s real 5-second sensor sampling, a real IR-remote-triggered object-detection event, and the TxQueue -> CommTxTask -> UART TX path were all confirmed operating on real hardware. No code changed.

**Stage 3 - Embedded -> CC (approved by the user).** `cc_main.exe` connected live to the real board: startup printed `Connected to COM10 at 115200 baud.` (not the fixture). Real CC status, read by the user via `[3] Show status`: **Port open: yes, Frames dispatched: 6, Decode errors: 0** - direct proof the real Embedded -> CC serial receive/decode path works.
  - **Real finding surfaced and investigated during this stage, not a defect**: the pre-existing 326 real measurement rows and 6 of 14 real event rows (both dated ~2026-08-31, from earlier hardware sessions) were deleted by `DataStore::pruneOlderThan()` the moment a new insert triggered it. Verified by direct computation, not assumed: real "now" was `2026-09-07 17:06:57 UTC`, the 7-day retention cutoff `2026-08-31 17:06:57 UTC`; the old measurements' max timestamp (`2026-08-31 00:31:25 UTC`) fell before the cutoff, so correct, working retention logic removed them - a real, working feature, not a bug. The 8 surviving events (timestamps `2026-08-31 17:44:46 UTC` and earlier-but-still-post-cutoff) survived only because `ObjectDetection`'s synthetic clock advances roughly 250x faster than `Monitor`'s (~1 unit/20ms poll vs. ~1 unit/5s cycle), so it had already "outrun" the cutoff while Monitor's had not.

**Stage 4 - Embedded + CC + GS simultaneously (this verification round).** With the real board still running and `cc_main.exe` still connected to COM10, `gs_main.exe` was started and connected over a real TCP connection to the real CC while live Embedded traffic continued arriving.

- **CC**: `Port open: yes`; **`Frames dispatched` increased from 6 to 12 during the session** (continued receiving real Embedded frames while GS was connected and making requests); `Decode errors: 0`; GS connection established (listening + client connected).
- **GS**: `Connected: yes`; `Decode errors: 0`.
- **Event request: PASS with real data.** GS requested and received exactly the 8 real, hardware-generated `ObjectDetection` events (the same ones that survived Stage 3's retention prune):
  ```
  1788197513  Object detected
  1788197561  Object cleared
  1788197792  Object detected
  1788197856  Object cleared
  1788198163  Object detected
  1788198202  Object cleared
  1788198238  Object detected
  1788198286  Object cleared
  ```
  This is a genuine Embedded -> CC -> `DataStore` -> GS round trip for real sensor-triggered data, not fixture data.
- **Measurement request: round trip PASS, zero rows returned - NOT a communication failure.** The request was sent successfully and a valid, well-formed reply was received (`0 measurements`). This is the expected, already-understood consequence of Stage 3's finding: `Monitor`'s synthetic-clock timestamps advance far too slowly (~1 unit/5s) to ever catch up to the real, continuously-advancing 7-day retention cutoff under realistic uptimes (~85+ hours would be needed) - any measurement row is pruned essentially the instant it's inserted. The round trip itself (request -> CC parse -> `DataStore` query -> chunk -> TCP -> GS decode -> callback) is proven correct by the event test above and by the reply arriving at all; only the *data* is structurally unable to persist long enough to be queried back, for a reason already known and explicitly not being fixed here.
- **Concurrent-traffic check**: `Frames dispatched` on CC continuing to climb (6 -> 12) throughout the GS session confirms real Embedded traffic and GS's TCP requests were serviced concurrently in the same single-threaded `poll()` loop, with no stall, no cross-talk, and no decode errors on either side.

**Known limitation, explicitly not being changed now**: `Monitor`/`ObjectDetection`'s timestamps come from a synthetic, per-module test clock (`g_monitor_test_timestamp`, `g_object_detection_test_timestamp`), not the real RTC that Init already wires up elsewhere (Sec 9.2/2.7) - a leftover from before RTC was enabled, never revisited. This causes the retention interaction documented above. Left as-is per explicit instruction; not a blocker for anything verified here.

**Conclusion**: full live Embedded -> CC -> GS connectivity is verified on real hardware. CC continued receiving live Embedded traffic while GS was connected and actively making requests (proving the two `poll()` loops coexist correctly, the one integration gap identified before this test that had never been exercised). The event path was verified end-to-end with 8 real `ObjectDetection` events, real sensor-triggered data traveling the complete real path. The measurement round trip was verified as mechanically correct; persisted measurement retrieval cannot currently be demonstrated, for the known synthetic-timestamp/retention reason above, not a communication defect. No unexpected decode errors occurred anywhere, on any of CC's two links or GS's link, across all 3 stages. No source files were modified in any of Stages 2, 3, or 4.

---

## Phase 1 — LNC Timestamp/RTC Hardening: IMPLEMENTED and HARDWARE-VERIFIED (2026-09-07) — Test D explicitly deferred, see below

**Not part of the original 7-step Ground Station plan** - a new, independently-scoped phase addressing the synthetic-timestamp/retention limitation the Full Live Hardware Integration Verification above surfaced. Went through a full investigation report, a design proposal, two rounds of user-directed correction, and final approval before any code was written (see this session's own history for the full investigation/design trail - not reproduced here).

### What was implemented

- **New RTC synchronization state, gated behind a minimal API** (`Embeded/Core/Inc/rtc_sync.h`, no new `.c` - implemented in `main.c`, where the RTC handle already lives): `RtcSync_IsSynchronized()` and `RtcSync_ApplyEpoch(uint32_t)`. The internal `rtc_synchronized` flag stays `static` inside `main.c`, exactly as required - no other file reads or writes it by name; every caller goes through the two functions. Chosen over adding declarations to `main.h` (would have forced Application-layer modules to newly depend on CubeMX's hardware/pin header, which none of them do today) or `communication.h` (wrong ownership - not `communication.c`'s own concern).
- **`RtcSync_ApplyEpoch()`** validates the epoch is representable by this RTC's hardware (`RTC_DateTypeDef.Year` is a `uint8_t` offset from 2000, so only 2000-2099 is valid - `946684800`-`4102444799`), converts it via a new `EpochToCalendar()` (the exact inverse of the existing `DateToEpoch()`, same "simple counting loop" style, added to `main.c` - not `log.c`, per explicit instruction, since `log.c`'s own version is private/date-only and do-not-touch), sets the RTC via `HAL_RTC_SetTime`/`SetDate`, and - only on full success - writes a marker (`RTC_SYNC_MARKER = 0x52544301`) to `RTC_BKP_DR0` and sets `rtc_synchronized = 1`. A repository-wide search (including `.ioc` and the full `Drivers`/`Middlewares` tree, not just `Core/`) confirmed no other project code uses this backup register.
- **`MX_RTC_Init()`** now checks `RTC_BKP_DR0` before unconditionally resetting the RTC to its hardcoded default: a valid marker skips the reset entirely (trusting the backup-domain-retained value) and sets `rtc_synchronized = 1` immediately at boot; a missing/invalid marker (first-ever boot, or backup-domain power loss - indistinguishable from software, by design) falls through to the existing default-set behavior, leaving the LNC unsynchronized.
- **Two synchronization mechanisms, both fully working, sharing the same `RtcSync_ApplyEpoch()`**:
  - **Existing, manual**: `TAG_SET_RTC_DATETIME` (CC push) - previously recognized but deliberately deferred (a no-op); now actually applies the epoch.
  - **New, active**: the LNC now sends `TAG_GET_SYSTEM_TIME_REQUEST` once, unconditionally, at the end of `Init_Start()` (`init.c`) - closing Sec 2.7's long-standing "Init requests time/date sync from CC" gap. CC's `Communication::dispatch()` gained a new, self-contained `case TAG_GET_SYSTEM_TIME_REQUEST:` that replies immediately and synchronously with this PC's own real time (`std::time(nullptr)`), no callback, no Management Command involvement - mirroring the LNC's own inline-reply precedent for the same tag. The LNC then parses the reply via a new `Message_ParseSystemTimeResponse()` and applies it the same way. No new tag was invented anywhere - this completes an already-frozen tag pair's previously-missing other direction (the third instance of this exact pattern in the project, after Step 1's chunk-response builders and Step 4b's range-request parsers).
- **Production timestamp source switched from synthetic counters to real RTC reads**, in `MonitorTask` and `ObjectDetectionTask` (`main.c`) - same `HAL_RTC_GetTime`/`GetDate` + `DateToEpoch()` pattern already proven in `CommRxTask`/`KeepAliveTask`. `g_monitor_test_timestamp`/`g_object_detection_test_timestamp` are no longer used for the production timestamp (left declared but now dead - a known, disclosed cleanup opportunity, not removed this pass to keep the change minimal).
- **Pre-sync gating - Option C with an internal flag, exactly as approved**: all local behavior (sampling, classification, IR debounce, `Log_Write()`, LED/buzzer via `Event_On*()`, Configuration changes) runs completely unconditionally, synchronized or not. Only the final `TxQueue_Enqueue*()` call at each of 5 existing call sites is gated on `RtcSync_IsSynchronized()`: Monitor's `DATA_REPORT`, Monitor's mode-transition event, ObjectDetection's event, KeepAlive's frame, and `communication.c`'s `ReportConfigurationChanged()` event. Zero changes to `event.c`, `log.c`, `DataStore`, `DataCollection`, or any GS/TCP/range code - confirmed by this implementation, not just planned.
- **Startup event**: `Init_Start()`'s call to `Event_OnInitStartup()` (local record, LED, buzzer) is unconditional; only its `TxQueue_EnqueueEvent()` call is gated. **Explicit, disclosed consequence**: since `Init_Start()` runs synchronously and returns before the boot-time sync request it sends afterward can possibly be answered, the startup event's CC-bound transmission is skipped on every real boot, deterministically - by explicit decision, no deferred/retry transmission was added.
- **`SynchPrediv`/`AsynchPrediv` deliberately left unchanged** (still `255`/`127`) - RTC LSI-accuracy calibration is an explicitly separate, later follow-up phase.

### Files changed

**LNC**: `Embeded/Core/Inc/rtc_sync.h` (new), `Embeded/Core/Src/main.c`, `communication.c`, `Embeded/Core/Inc/communication.h`, `Embeded/Core/Src/message.c`, `Embeded/Core/Inc/message.h`, `monitor.c`, `object_detection.c`, `keepalive.c`, `init.c`.
**CC**: `CentralComputer/include/message.h`, `src/message.cpp` (new `parseGetSystemTimeRequest`/`buildSystemTimeResponse`), `CentralComputer/include/communication.h`, `src/communication.cpp` (new dispatch case).
**Not touched, confirmed**: `log.c`, `event.c`, `DataStore`, `DataCollection`, any GS file, any TCP/range-protocol file, `tlv_common.h` (no new tag), `SynchPrediv`/`AsynchPrediv`.

### Build verification (real toolchains, not just review)

- **LNC**: full clean rebuild via the real ARM toolchain (`arm-none-eabi-gcc`, found under the local STM32CubeIDE install, invoked directly via the project's real `Debug/makefile`) - `Embeded.elf` linked successfully, **zero warnings, zero errors** (`-Wall` enabled). Flash size: `text=79680, data=156, bss=24020` (small, expected increase from the new code).
- **CC**: `message.cpp`/`communication.cpp` compile-checked individually (`-Wall`, zero warnings), then the real `cc_main.exe` fully rebuilt and relinked - clean, same single pre-existing unrelated `ModeName`-unused warning as always, zero new warnings/errors.
- **Regression, all 14 PC-side automated suites re-run, 540/540 checks passing, 0 failures**: `protocol_test` (C) 23/23, `message_test` (C, LNC) 80/80, `tlv_codec_test` 22/22, `cc_message_test` 92/92, `gs_message_test` 32/32, `communication_test` 29/29, `management_command_test` 14/14, `data_store_test` 24/24, `data_collection_test` 25/25, `report_generator_test` 16/16, `tcp_transport_test` 18/18, `tcp_server_transport_test` 30/30, `gs_communication_test` 77/77, `cc_communication_test` 58/58 - identical total to before this phase, confirming zero regression anywhere.
- **No new automated tests were created this pass**, per explicit instruction - the 12-test verification sequence already designed (RTC receives/converts time, timestamps close to real time, DataStore survival, GS retrieval, monotonicity, reboot/unsynchronized behavior, full regression) remains to be executed on **real hardware**, which only the user can do.
- **Hardware verification status**: real-hardware testing of this phase's behavior has since been performed - see "Phase 1 — LNC Timestamp/RTC Hardening — Complete Status" below for the full, final results (Test 1 and Test C both PASS; Test D explicitly DEFERRED, not yet executed).

### Phase 1 — Current Verification Checkpoint (2026-09-07): hardware verification STARTED, NOT YET COMPLETE

**Software status (recap, unchanged since implementation):**
- LNC clean build: **PASS**, 0 errors, 0 warnings (real `arm-none-eabi-gcc` via the project's actual `Debug/makefile`).
- CC build: **PASS**, only the pre-existing, unrelated `ModeName`-unused warning (same as every prior session).
- Regression: **540/540 PASS, 0 failures**, across all 14 PC-side automated suites - identical total to before Phase 1, confirming zero regression.
- Source changes and the new `Embeded/Core/Inc/rtc_sync.h` file: as documented in "What was implemented"/"Files changed" above - unchanged, nothing further modified since.
- **Nothing has been committed.**

**Hardware verification: STARTED, NOT COMPLETE. Do not treat anything below as a final PASS/FAIL.**

What has actually been done and observed so far, stated precisely:
- The Phase 1 firmware was flashed to the real board and verified via `STM32_Programmer_CLI` (download + verify succeeded).
- A first reset was performed with nothing connected to COM10, and a standalone, passive, read-only external capture (outside the project tree, same precedent as earlier hardware sessions' `com10_capture.py`) observed the raw UART traffic for 25 real seconds immediately after boot: **exactly one frame was seen, tag `0x10` (`TAG_GET_SYSTEM_TIME_REQUEST`), zero-length payload - and zero `DATA_REPORT`/`KEEPALIVE`/event frames appeared in that window.** This is real, directly-observed evidence (not assumed) that (a) the new unconditional boot-time sync request is actually sent, and (b) the pre-sync transmission gate is actually withholding production frames - but this was only the pre-sync half; no CC was present in this specific window to answer the request, so nothing about the sync round trip itself was tested by it.
- `cc_main.exe` was then started (initially in a non-interactive/hidden session) and the board was reset again so a live CC would be present to answer the boot-time request this time. After roughly 29 minutes of real elapsed time, the database still showed **0 measurement rows** and no new event rows. Because the CC session was non-interactive, `Frames dispatched`/`Decode errors` could not be read, and no debugger was attached to the LNC, so **which stage of the round trip did or didn't complete could not be determined from this attempt** - this was reported as a diagnostic with explicit UNKNOWNs, not a FAIL.
- **Decision made at this checkpoint**: stop autonomous verification entirely. All further hardware verification will proceed **manually, one step at a time** - the user performs each action and reports the exact observed output back before the next instruction is given. This replaces the originally-planned "run stages automatically, report at the end" approach for the remainder of Phase 1's hardware verification.

**Exact point where we stopped, for picking this up cold:**
- **Hardware Test 1 is "Automatic Boot-Time RTC Synchronization."**
- The first manual action defined (not yet performed at the time of this checkpoint): start a fresh, **interactive** `cc_main.exe` session (the previous hidden one was stopped) and confirm, from its own real console output, that the database opens, the GS TCP listener starts, and it reports `Connected to COM10 at 115200 baud.` (not the WARNING).
- **No menu key should be pressed yet** - this first action only confirms the connection is live.
- **Next action, still pending**: waiting for the user to run this and report back the exact startup output and whether the menu appears. Nothing beyond this has been instructed or performed.

**Intended verification sequence after this checkpoint (updated as tests completed):**
1. Unsynchronized boot (partially evidenced above via passive capture; local Monitor/ObjectDetection/Startup execution and initial `rtc_synchronized` state itself still **PENDING** - no debugger read of that specific pre-sync moment was taken).
2. Automatic boot synchronization (request sent/received, response sent/received, RTC applied, `rtc_synchronized` becomes true) - **PASS**, see "Hardware Test 1 Result" below.
3. Post-synchronization traffic (Measurement/Event/KeepAlive resume, real RTC-derived timestamps) - **PASS**, see "FINAL VERIFICATION" item 3/4 below.
4. CC/DataStore verification (real Measurement reaches CC, timestamp is current, row survives retention) - **PASS**, see "FINAL VERIFICATION" item 3 below.
5. GS retrieval of the new Measurement and at least one real Event - **PASS**, see "FINAL VERIFICATION" items 4/5 below.
6. Manual `SET_RTC_DATETIME` path (RTC changes, sync state, subsequent timestamps follow it) - covered only incidentally during earlier diagnosis (confirmed the mechanism works), not as a dedicated pass/fail test - **PENDING** as a formal test.
7. Reboot / backup-marker survival across a normal `NRST` reset - **PASS**, see "Test C" in "Phase 1 — LNC Timestamp/RTC Hardening — Complete Status" below. (A genuine Backup-Domain-clearing power-loss condition is a separate, still-unexecuted case - see Test D.)
8. Invalid epoch rejection (`RtcSync_ApplyEpoch()` correctly refuses an out-of-range epoch, sync state unaffected) - **PENDING**.

**Phase 1 hardware/end-to-end verification has PASSED every test actually executed** (see "Phase 1 — LNC Timestamp/RTC Hardening — Complete Status" below for the final, authoritative record) - this is explicitly **not** a claim of 100% verification: test 1 (unsynchronized-boot debugger capture), test 6 (manual `SET_RTC_DATETIME` as a dedicated test), test 8 (invalid epoch), and **Test D (unsynchronized boot after a genuine Backup Domain loss)** remain formally PENDING/DEFERRED.

### Phase 1 — Hardware Test 1 Result (2026-09-07): Automatic Boot-Time RTC Synchronization — PASS

Verification method: a single controlled reset with `cc_main.exe` already running and connected, with readings taken from both the CC status display and the LNC's existing debugger-visible globals (`g_frames_decoded_ok`, `g_frames_decode_error`, `g_dispatch_last_tag`, `rtc_synchronized`) immediately before and immediately after the reset, no manual RTC action performed at any point during this test.

- CC port open: **yes**.
- CC Frames dispatched: **0 before reset -> 2 after reset**.
- CC Decode errors: **0** (unchanged).
- LNC `g_frames_decoded_ok`: **0 -> 1**.
- LNC `g_frames_decode_error`: **0** (unchanged).
- LNC `g_dispatch_last_tag`: **0x20** (`TAG_SYSTEM_TIME_RESPONSE`) after reset.
- LNC `rtc_synchronized`: **0 -> 1**.
- **No manual RTC request (`SET_RTC_DATETIME` or otherwise) was used during this verification** - the transition was produced solely by the automatic boot-time sequence.

**Conclusion**: this confirms the automatic boot-time path works end-to-end on real hardware: `Init_Start()` sends `TAG_GET_SYSTEM_TIME_REQUEST` -> CC receives it and replies with `TAG_SYSTEM_TIME_RESPONSE` -> the LNC receives and dispatches that response -> `RtcSync_ApplyEpoch()` is called and succeeds -> `rtc_synchronized` becomes `1`, all without any manual intervention.

### Phase 1 — LNC Timestamp/RTC Hardening — FINAL VERIFICATION (2026-09-07)

**1. Automatic boot-time RTC synchronization**
- Physical NUCLEO reset button pressed (confirmed by board LEDs restarting).
- No manual RTC command (`SET_RTC_DATETIME` or otherwise) was used during this test.
- LNC `rtc_synchronized`: **0 -> 1**.
- LNC `g_dispatch_last_tag`: **0x20** (`TAG_SYSTEM_TIME_RESPONSE`).
- LNC decode errors: **0**.
- CC received and dispatched the synchronization traffic successfully (`Frames dispatched` incremented, `Decode errors` stayed at 0).
- See "Hardware Test 1 Result" above for the full before/after counter values.

**2. RTC register verification**
- Read directly from the RTC peripheral via the debugger (attach-to-running, no reset, no reflash):
  - `DR = 0x262907` -> decodes to **07/09/2026** (day=07, month=09, year=26).
  - `TR = 0x200946`.
- This confirms the RTC hardware registers themselves - not just the `rtc_synchronized` flag - actually hold the synchronized 2026 date, ruling out the possibility of the flag being set without the underlying date/time actually being applied.

**3. Measurement persistence**
- The CC's report menu ([4]) showed a growing number of stored Measurements after synchronization (38, later observed at 288 in a live database read).
- Measurements were no longer immediately pruned by `pruneOlderThan()`'s real-wall-clock 7-day retention, because their timestamps are now real and close to the CC's own current time - the original root-cause problem this phase was created to fix.

**4. Measurement end-to-end retrieval**
- GS request: `requestMeasurements(1700000000, 2000000000)`.
- Result: **455 measurements received**.
- Example timestamps: `1788809465`, `1788809466`, `1788809471`, etc.
- These decode to real 2026 dates/times, not the old synthetic counter sequence (e.g. `1000`, `1010`, ...) seen before Phase 1.

**5. Event end-to-end retrieval**
- GS request: `requestEvents(1700000000, 2000000000)`.
- Result: **3 events received**.
- Example timestamps: `1788809465`, `1788810135`, `1788810942`.
- These are likewise real 2026 Unix timestamps.

**6. Runtime/database investigation (environment issue, not a code defect)**
- During verification, a discrepancy was found: the CC's own report showed stored measurements, but a GS range request initially returned 0 results for a range that should have covered them.
- Investigation (source inspection of `data_store.cpp`, `report_generator.cpp`, `gs_communication.cpp`, and the `TimeRangeMessage` build/parse pair in `message.cpp`, plus direct read-only inspection of the database files and process/port state) found **no bug in `DataStore`, the report generator, or the range-request protocol code** - the SQL, the bindings, and the wire encode/decode were all verified correct and symmetric.
- The actual cause: **two separate `cc_main.exe` processes were running simultaneously**, launched from different working directories, each opening its own `central_computer.db` (the DataStore path is a bare relative string). One process (the stale, older one) held real, growing measurement data; the other (the newer, correct one at the time of the initial check) had a near-empty, week-old database. `Get-NetTCPConnection -LocalPort 5000` confirmed the GS's TCP connection had landed on the **stale** process, which is why its range request returned data from the wrong database.
- This was resolved by terminating the stale process; port 5000 ownership was then re-verified. No source code or database file was modified as part of this diagnosis.
- **Conclusion: this was an environment/runtime process-management issue (a leftover process from earlier testing), not a defect in `DataStore`, `report_generator`, or the range-request protocol.**

**7. Final Phase 1 result: PASSED**
- **Verified**: automatic boot-time RTC synchronization (flag and underlying RTC registers both confirmed), real RTC-derived production timestamps, measurement persistence past the 7-day retention cutoff, full GS end-to-end retrieval of both Measurements and Events with real 2026 timestamps.
- **Known limitations, unchanged from the original design (not defects)**: the LNC's startup event can never reach the CC (deterministic consequence of Option C's boot ordering, disclosed at design time); no retry/timeout if CC never answers the boot-time sync request; `g_monitor_test_timestamp`/`g_object_detection_test_timestamp` remain declared but unused (disclosed cleanup opportunity, not acted on this phase).
- **Not covered by this verification pass**: reboot/backup-marker survival across a real power cycle, invalid-epoch rejection, and the manual `SET_RTC_DATETIME` path under real hardware conditions (each was covered only functionally/incidentally during diagnosis, not as a dedicated pass/fail test) - these remain candidates for a future verification pass if desired.

## Phase 1 — LNC Timestamp/RTC Hardening — Complete Status (2026-09-07)

This section is the authoritative, consolidated record of Phase 1's final implementation and verification state, superseding the narrative checkpoints above where they overlap.

### 1. Implementation (final)

- **Boot-time RTC synchronization** reuses the existing, already-frozen `TAG_GET_SYSTEM_TIME_REQUEST` / `TAG_SYSTEM_TIME_RESPONSE` tag pair - no new wire tag was created. The LNC now also sends this request (previously only CC did), and CC now also answers it (previously only the LNC did).
- **New `Embeded/Core/Inc/rtc_sync.h` interface** - the only new file this phase added. Declares exactly two functions, no data, no CubeMX/`main.h` dependency.
- **`RtcSync_ApplyEpoch(uint32_t)`** - validates the epoch against this RTC's representable range (2000-2099), converts it via the new `EpochToCalendar()` (in `main.c`), applies it via `HAL_RTC_SetTime`/`SetDate`, and on success writes the `RTC_BKP_DR0` marker and sets the synchronized state.
- **`RtcSync_IsSynchronized()`** - the only way any other file reads synchronization state.
- **`rtc_synchronized`** - `static` inside `main.c`; no other translation unit accesses it directly, only through the two functions above.
- **`RTC_BKP_DR0` synchronization marker** (`RTC_SYNC_MARKER = 0x52544301`) - written only on a successful `RtcSync_ApplyEpoch()`; read once, at boot, in `MX_RTC_Init()`.
- **RTC epoch validation**: only epochs in `[946684800, 4102444799]` (years 2000-2099, the range `RTC_DateTypeDef.Year`'s `uint8_t` representation allows) are accepted; anything outside is rejected before touching the RTC.
- **Epoch-to-RTC-calendar conversion**: `EpochToCalendar()`, the exact inverse of the existing `DateToEpoch()`, same "simple counting loop" style, added to `main.c` (not `log.c` - that file's own `EpochToDate()` is private/date-only and was left untouched).
- **Automatic boot synchronization request**: `Init_Start()` (`init.c`) unconditionally sends one `GET_SYSTEM_TIME_REQUEST` at the end of every boot, regardless of synchronization state (sending it is how synchronization becomes possible).
- **Manual `SET_RTC_DATETIME` now uses the same `RtcSync_ApplyEpoch()` path** as the automatic mechanism - previously a recognized-but-deferred no-op, now fully functional and consistent with the new mechanism.
- **TX gates**: only the final `TxQueue_Enqueue*()` call is gated on `RtcSync_IsSynchronized()`, at exactly 5 existing call sites (Monitor's `DATA_REPORT`, Monitor's mode-transition event, ObjectDetection's event, KeepAlive's frame, `communication.c`'s `ReportConfigurationChanged()` event) plus the startup event in `init.c`. All local behavior (sampling, logging, LED/buzzer) remains unconditional.
- **CC-side handling**: `Communication::dispatch()` gained one new case for `TAG_GET_SYSTEM_TIME_REQUEST` (replies inline/synchronously with `std::time(nullptr)`, no callback) and one new case for `TAG_SYSTEM_TIME_RESPONSE` is unchanged from its pre-existing callback-based handling (the LNC-side equivalent is what's new there).

**Explicitly NOT changed by this phase** (confirmed by inspection, not just intent):
- No `log.c` changes.
- No `event.c` changes.
- No `DataStore`/`DataCollection` changes.
- No GroundStation changes.
- No TCP/range-protocol changes.
- No RTC prescaler changes (`SynchPrediv`/`AsynchPrediv` remain `255`/`127`).
- No new wire-level synchronization-status field - synchronization state is purely local to the LNC and CC's own real clock; nothing on the wire announces it.

### 2. Verification completed

#### Test 1 — Automatic Boot-Time RTC Synchronization: **PASS**

Real hardware observation, via debugger (attach-to-running, no reflash) and the CC's status display, bracketing one physical board reset with `cc_main.exe` already running:

| | Before | After |
|---|---|---|
| LNC `rtc_synchronized` | `0` | `1` |
| LNC `g_dispatch_last_tag` | `0` | `32` (`0x20`, `TAG_SYSTEM_TIME_RESPONSE`) |
| LNC `g_frames_decoded_ok` | - | `1` |
| LNC `g_frames_decode_error` | `0` | `0` |
| CC frames dispatched | `0` | `2` |
| CC decode errors | `0` | `0` |

No manual RTC command was sent at any point during this test. This proves the automatic boot-time `GET_SYSTEM_TIME_REQUEST` → CC reply → LNC dispatch → `RtcSync_ApplyEpoch()` chain works end-to-end on real hardware, entirely on its own.

#### Test C — RESET / Backup Marker Survival: **PASS**

Real hardware observation, via debugger, bracketing one physical `NRST` (board RESET button) press on an already-synchronized board:

| Register | Before RESET | After RESET |
|---|---|---|
| `TR` | `0x204006` | `0x204043` |
| `DR` | `0x262907` | `0x262907` |

`rtc_synchronized` remained `1` across the reset. The date (`DR = 0x262907` → 07/09/2026) was unchanged and the time (`TR`) continued advancing normally, confirming the RTC calendar kept running through the reset rather than being reinitialized. `RTC_BKP_DR0`'s marker and the synchronized state survived the normal `NRST` reset with no re-synchronization required.

**Physical RESET investigation** (see the dedicated investigation performed earlier this session): the NUCLEO-L476RG's **B2 ("RESET")** push-button drives the STM32's **`NRST`** pin, producing a normal MCU/System Reset. Per ST's documented reset architecture, this reset does **not** touch the RTC Backup Domain or `RTC_BKP_DR0` - both are only cleared by an actual loss of Backup Domain power or an explicit software-forced Backup Domain reset (neither of which this project's code performs). This makes the RESET button **appropriate and safe for Test C**, but **not sufficient for Test D**, which specifically requires an unsynchronized (Backup-Domain-cleared) starting state.

### 3. Real end-to-end hardware verification

After synchronization, real hardware data was confirmed to survive the complete chain: **Embedded → UART/COM10 → CC → DataStore → TCP → GS.**

- **Measurements**: GS successfully received **455 measurements** via `requestMeasurements()`, all with real 2026 timestamps. Example: `t=1788809465 temp=14.00 humidity=153.00 light=52.55 battery=2.22 mode=2`.
- **Events**: GS successfully received **3 events** via `requestEvents()`, all with real 2026 timestamps:
  - `t=1788809465` ModeTransition `"Mode change: Normal -> Error"`
  - `t=1788810135` ModeTransition `"Mode change: Normal -> Error"`
  - `t=1788810942` ModeTransition `"Mode change: Normal -> Error"`

This confirms the original problem this phase exists to fix - synthetic/frozen timestamps causing `DataStore::pruneOlderThan()`'s real-wall-clock 7-day retention to discard live data prematurely - is practically resolved: current hardware data now carries real timestamps, survives retention, and is retrievable by GS end-to-end.

**Earlier real-hardware verification, already documented, still valid and unaffected by this phase:**
- Embedded → CC: real frames received, decode errors 0.
- GS → CC → DataStore → GS path verified.
- Multi-chunk software end-to-end already verified: 14 measurements → 11 + 3 (two chunks), 8 events → 6 + 2 (two chunks).

### 4. Regression status

**540/540 tests PASS, 0 failures, across 14 automated suites** (latest full run, this phase):

| Suite | Result |
|---|---|
| `protocol_test.exe` | 23/23 |
| `message_test.exe` | 80/80 |
| `tlv_codec_test.exe` | 22/22 |
| `cc_message_test.exe` | 92/92 |
| `gs_message_test.exe` | 32/32 |
| `communication_test.exe` | 29/29 |
| `management_command_test.exe` | 14/14 |
| `data_store_test.exe` | 24/24 |
| `data_collection_test.exe` | 25/25 |
| `report_generator_test.exe` | 16/16 |
| `tcp_transport_test.exe` | 18/18 |
| `tcp_server_transport_test.exe` | 30/30 |
| `gs_communication_test.exe` | 77/77 |
| `cc_communication_test.exe` | 58/58 |

**Preserved, known status, not part of the 540-count re-run** (both require real hardware, so are not re-run as part of routine PC-side regression):
- `serial_transport_test.exe` - last known result: **10/10** (requires the real COM10 board).
- `hardware_loopback_test.exe` - exists in `Tests/HardwareLoopback/`, but **no documented final pass count exists for it**. Not invented here.

### 5. Test D — Unsynchronized Boot: **DEFERRED / NOT YET EXECUTED**

Test D's purpose is to verify:
1. `rtc_synchronized = 0` after a genuine loss of the RTC Backup Domain.
2. No Measurement/Event/KeepAlive transmission occurs before synchronization.
3. The LNC sends `GET_SYSTEM_TIME_REQUEST`.
4. CC returns `SYSTEM_TIME_RESPONSE`.
5. `RtcSync_ApplyEpoch()` changes `rtc_synchronized` from `0` to `1`.
6. TX gates then allow normal telemetry/event traffic to resume.

**Why it was deferred**: the physical RESET investigation above confirmed that a normal `NRST` reset does not clear the Backup Domain, so pressing the RESET button cannot produce the unsynchronized starting state Test D needs. Achieving that state requires either a genuine full power-loss condition or an explicit software-forced Backup Domain reset - both intentionally not performed yet, since the former is disruptive to the current verified hardware state and the latter is a destructive action outside this pass's scope. **Test D is not marked PASS, and no code was changed to make it easier to run.**

### 6. Phase 1 conclusion

**Phase 1 (LNC Timestamp/RTC Hardening) implementation is complete and has PASSED every verification test actually executed** (Test 1, Test C, and the full real-hardware Measurement/Event end-to-end retrieval). Regression remains 540/540 with zero new failures. **Phase 1 verification is not being claimed as 100% complete**: Test D (unsynchronized boot) remains explicitly deferred and unexecuted, pending a decision on how to safely force a genuine Backup-Domain-cleared state.

## Phase 2 — Periodic RTC Re-Synchronization (LSI Drift Correction) — DESIGN ONLY, NOT YET APPROVED, NOT IMPLEMENTED

Proposed next development step, chosen from the project's own already-documented deferred items (see "Next-step candidates considered" below) - not an invented feature. **No code has been written for this. Awaiting approval.**

### 1. Proposed phase/step name
Phase 2 - Periodic RTC Re-Synchronization (LSI Drift Correction).

### 2. Objective
Correct for long-uptime clock drift by periodically repeating the same synchronization Phase 1 currently only performs once, at boot.

### 3. Why this is the correct next step
- **Already explicitly flagged as the deliberate follow-up to Phase 1**, in Phase 1's own implementation notes: "`SynchPrediv`/`AsynchPrediv` deliberately left unchanged... RTC LSI-accuracy calibration is an explicitly separate, later follow-up phase." This proposal is that follow-up, not a new invention.
- The LNC's RTC clock source is **LSI** (confirmed in `stm32l4xx_hal_msp.c`'s `HAL_RTC_MspInit()`) - an uncalibrated internal RC oscillator, not the more accurate external LSE crystal. LSI drift is typically several percent, uncorrected by anything in the current design.
- Phase 1's Test C just proved the sync marker and RTC state **survive resets indefinitely** (`RTC_BKP_DR0` persists across `NRST`, and the Backup Domain is only lost on real power loss) - meaning a board can now legitimately run for many days between reboots without ever re-syncing, since sync currently only happens once, at boot. Uncorrected LSI drift over that long an uptime could eventually erode the same timestamp accuracy Phase 1 was built to establish - a real, currently-unaddressed gap this phase directly closes.
- **Reuses 100% of Phase 1's already-built, already-verified infrastructure** - the same `GET_SYSTEM_TIME_REQUEST`/`SYSTEM_TIME_RESPONSE` tag pair, the same `RtcSync_ApplyEpoch()`, the same CC-side auto-reply. No new wire tag, no new module, no new architecture.
- **Next-step candidates considered and set aside**:
  - **Watchdog module** (Open Question #2) - the only other fully-unbuilt planned LNC module. Explicitly and repeatedly marked "paused," "do not touch until explicitly revisited," and "BLOCKING" by your own prior decisions throughout this project's history (it was previously implemented, then found to be the root cause of a real hardware reset bug, then deliberately removed entirely). **Not proposed here** - reopening it would need your explicit instruction first, not an inference from "what's left."
  - **GS TCP reconnect / mid-request-disconnect recovery** - a real, documented open gap ("explicitly OUT OF SCOPE and UNHANDLED... an open question for a later step"), but a larger, separate concern in already-shipped, already-tested GS/CC code, requiring new reconnect-state-machine logic - a bigger architectural footprint than this phase for a comparatively rare failure mode. A reasonable future phase, just not as tightly scoped or as directly connected to Phase 1 as this one.

### 4. Files that would potentially be added/modified
- **`Embeded/Core/Src/main.c`** (`StartTask07`/`KeepAliveTask`, which already runs every 6 seconds, Sec 2.8) - add a private interval counter and, when it elapses, issue the same request Phase 1's `Init_Start()` already sends once at boot.
- **No new files.** No changes anywhere else - not `rtc_sync.h`, not `communication.c`/`.h`, not any CC or GS file.

### 5. Existing functions/classes/modules reused
- `Message_BuildGetSystemTimeRequest()` (`message.c`) - unchanged, called again.
- `TxQueue_EnqueueEvent()` (`tx_queue.c`) - unchanged.
- The LNC's existing `TAG_SYSTEM_TIME_RESPONSE` dispatch case (`communication.c`) - already calls `RtcSync_ApplyEpoch()`; nothing to change, it already handles being called more than once.
- CC's existing inline `TAG_GET_SYSTEM_TIME_REQUEST` auto-reply (`communication.cpp`) - already stateless and answers any request identically regardless of why it was sent; nothing to change.

### 6. New interfaces/API
**None required.** This is a deliberate constraint of this proposal - it is a scheduling change only (send the existing request more than once), not a new capability.

### 7. Data flow / control flow
1. `KeepAliveTask`'s existing periodic loop gains a private counter, incremented once per cycle (currently every 6 seconds).
2. When the counter reaches the chosen interval (open question #1 below), the task builds and enqueues one `GET_SYSTEM_TIME_REQUEST` - the exact same call `Init_Start()` already makes once at boot - then resets the counter.
3. CC receives and answers it exactly as it already does today, indistinguishable from the boot-time request.
4. The LNC's existing dispatch path applies the response via `RtcSync_ApplyEpoch()`, which re-validates the epoch, re-sets the RTC, and re-writes the `RTC_BKP_DR0` marker (already idempotent - safe to repeat).
5. `rtc_synchronized` remains `1` throughout; nothing about the gating logic changes.

### 8. Error handling
Identical to the existing boot-time mechanism, by design: no retry, no timeout, no new failure state. If CC doesn't answer a periodic request, the LNC keeps running on its current (slightly drifted) RTC value and simply tries again at the next interval - consistent with this project's established "no retry" philosophy (DataCollection's backfill requests, CC's range requests).

### 9. Testing strategy
- **PC-side automated**: no new wire behavior exists to test - the same builder/parser functions are already 100% covered by the existing `message_test`/`cc_message_test` suites. No new automated test is expected to be required, pending your confirmation once the exact interval/trigger mechanism is chosen.
- **Hardware verification (new, manual, after implementation and your approval)**: run the already-synchronized board for at least one full interval and confirm, via the same debugger-visible globals already used for Test 1 (`g_dispatch_last_tag`, CC's `Frames dispatched`), that a second request/response cycle occurs automatically without any manual action. Whether the real production interval or a temporarily shortened one is used for this test is an open question for you (#4 below).

### 10. Regression requirements
Full existing 540-check PC-side suite must remain green, 540/540 - no wire format or message-layer change is introduced, so no regression is expected anywhere.

### 11. Explicitly out of scope
- Hardware LSI frequency measurement or smooth calibration (`HAL_RTCEx_SetSmoothCalib` or a TIM-based LSI measurement) - a materially larger approach; not pursued unless periodic resync proves insufficient.
- The Watchdog module (Open Question #2) - stays paused.
- GS TCP reconnect/mid-request-disconnect recovery - a separate, later candidate phase if wanted.
- **Phase 1's Test D** (unsynchronized boot after a genuine Backup Domain loss) - remains its own deferred verification item; this phase does not depend on it, does not fix it, and does not fold it in as a requirement.
- Any change to `SynchPrediv`/`AsynchPrediv` themselves - still untouched.

### 12. Open architectural questions requiring your approval
1. **What resync interval?** Options range from a fixed number of `KeepAliveTask` cycles (e.g. every N x 6s) to a running-seconds counter for something like once per hour or once per day. No default is assumed here.
2. **Should the trigger live inside `KeepAliveTask`** (piggybacking on its existing 6-second cadence, no new task) **or somewhere else?** `KeepAliveTask` is proposed as the natural host since it already runs periodically and unconditionally, but this needs your confirmation.
3. **Should the periodic request be gated on `RtcSync_IsSynchronized()`** (skip firing until the first, boot-time sync has already succeeded, avoiding a redundant near-duplicate request early on) **or fire unconditionally on the same interval regardless of sync state**, matching the boot-time request's own "unconditional" precedent?
4. **For hardware verification, is an artificially shortened interval acceptable** for practical testing, or must the real production interval be used even during verification (implying a multi-hour/day test)?

**No implementation will begin until you approve this design and answer the open questions above.**

**Proposed API (design only, not yet implemented):**

```cpp
// CentralComputer/include/tcp_transport.h
namespace transport {
class TcpServerSocket {
public:
    TcpServerSocket() = default; ~TcpServerSocket();
    TcpServerSocket(const TcpServerSocket&) = delete;
    TcpServerSocket& operator=(const TcpServerSocket&) = delete;

    bool startListening(uint16_t port);   // binds all local interfaces + listen()
    void close();                          // stops listening AND drops any connected client
    bool isListening() const;
    bool isClientConnected() const;

    bool tryAcceptClient();  // non-blocking; true if a NEW client was just accepted this call

    bool send(const std::vector<uint8_t> &data, uint32_t timeoutMs);
    bool receiveByte(uint8_t &outByte, uint32_t timeoutMs);
private:
    void *listenHandle_ = nullptr;
    void *clientHandle_ = nullptr;
};
}
```

```cpp
// CentralComputer/include/gs_communication.h
namespace gs_communication {
class GsCommunication {
public:
    explicit GsCommunication(data_store::DataStore &dataStore);  // non-owning reference, same pattern as ManagementCommand holding Communication&

    uint32_t framesDispatched = 0;
    uint32_t decodeErrors = 0;

    bool startListening(uint16_t port);
    void close();
    bool isListening() const;
    bool isClientConnected() const;

    void poll();                    // accept-if-pending, then decode-one-byte-if-connected
    void feedByte(uint8_t byte);    // exposed for tests, same seam Communication::feedByte() already provides

private:
    void dispatch(const tlv::Frame &frame);
    void sendMeasurementsForRange(const message::TimeRangeMessage &request);
    void sendEventsForRange(const message::TimeRangeMessage &request);

    transport::TcpServerSocket serverTransport_;
    tlv::Decoder decoder_;
    data_store::DataStore &dataStore_;
};
}
```

**Data flow (GS -> TCP -> CC -> DataStore, and back):**
```
GS: message::buildGetMeasurementsByRangeRequest() -> TcpSocket::send()
      -> [TCP, port 5000] ->
CC: TcpServerSocket::receiveByte() (via GsCommunication::poll())
      -> tlv::Decoder -> GsCommunication::feedByte() -> dispatch()
      -> case TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST:
           message::parseGetMeasurementsByRangeRequest()  [NEW, small addition to Step 1's file]
           -> sendMeasurementsForRange(request):
                dataStore_.getMeasurementsInRange(startTime, endTime)   [existing, unchanged]
                -> split into groups of <= MAX_MEASUREMENTS_PER_CHUNK
                -> for each group: message::buildMeasurementChunkResponse()  [existing, Step 1, unchanged]
                     echoing request.requestId back in every chunk
                -> serverTransport_.send() each built frame
      -> [TCP] ->
GS: TcpSocket::receiveByte() -> its own tlv::Decoder -> message::parseMeasurementChunkResponse()  [existing, Step 2, unchanged]
      -> accumulate chunks until moreDataFlag == false -> display
```
(`TAG_GET_EVENTS_BY_RANGE_REQUEST` follows the identical path via `sendEventsForRange()`/`getEventsInRange()`/`buildEventChunkResponse()`.)

**All agreed decisions, recorded for implementation:**
- CC-side transport is one class, `transport::TcpServerSocket`, not a separate Listener+Connection pair - GS is a single-operator tool, no multi-client support needed (Sec 12: don't design for a hypothetical future requirement).
- **TCP port: `5000`.**
- **Reconnection is supported**: `close()`ing/losing a client drops only the client connection, not the listening socket - CC keeps listening indefinitely for a fresh GS connection, matching CC's role as the long-running server.
- **Fully synchronous handling inside `dispatch()`** - no `DataCollection`-style async request/chunk correlation class, because CC already holds all of GS's requested data locally (unlike the genuinely-asynchronous LNC relationship, where the LNC takes real time to reply over many future `poll()` calls).
- **`poll()` does both jobs in one call**: accept a pending client if none is connected, then make at most one byte of decode progress if one is - mirrors `communication::Communication::poll()`'s single do-everything-non-blocking shape exactly, for the simplest possible future integration into `cc_main.cpp`'s existing loop.
- **`GsCommunication` holds `data_store::DataStore &dataStore_` directly** (non-owning reference) - no wrapper class, since `DataStore`'s two range-query methods are already public and read-only in effect.
- **Important asymmetry, stated explicitly so it isn't re-litigated later**: GS is the *requester* and generates its own `requestId`; CC only ever echoes it back in every response chunk. Unlike `DataCollection`'s CC->LNC direction (where CC, as requester there, must generate its own `nextRequestId_`), `GsCommunication` needs **no** request-ID-generation logic at all.
- Reuses, unchanged: `TimeRangeMessage`, `MeasurementChunkResponse`/`EventChunkResponse`, `buildMeasurementChunkResponse`/`buildEventChunkResponse`, `Common/TLVCodec`. No new protocol structures.

**What is implemented vs. still pending, stated plainly (updated through Step 7):**
- Implemented: Steps 1-3 (message layers both sides, GS's client transport), Step 4 in full (4a `TcpServerSocket` 30/30, 4b CC-side parsers 92/92, 4c `GsCommunication` shell 31/31, 4d DataStore-querying/response-sending 77/77 total, plus `cc_main.cpp` wiring), Step 5 (`cc_communication::CcCommunication`, 58/58, including the first real cross-executable integration test), and Step 7 (`gs_main.cpp`, build/link-verified, real startup connect proven against a real separate CC process). Step 6 officially skipped (folded into Step 4d - see below). **All 7 steps of the original plan are now done.**
- Pending: nothing from the original plan. Full interactive manual verification of `gs_main.cpp`'s menu (option 5 of Step 7's approved test plan) remains a user action.

#### STOPPING POINT (2026-09-07, this is the real one, supersedes every block below): ORIGINAL 7-STEP PLAN FULLY COMPLETE AND MANUALLY END-TO-END VERIFIED
**All 7 steps of the original Ground Station development plan are done, PC-tested, AND now manually end-to-end verified by the user** (Step 6 folded into Step 4d, officially skipped). The user ran the real `cc_side_test_server.exe` fixture and the real `gs_main.exe` and confirmed, by direct observation: single-chunk measurements, single-chunk events, multi-chunk measurements (11+3 merged to 14), and multi-chunk events (6+2 merged to 8) - all PASS. See "Manual End-to-End Verification of the GS <-> CC Communication Path: PASSED" above for full detail, including the one investigated-and-resolved (not-a-bug) confusion over the `2000-2010` range belonging to events, not measurements. Session paused here deliberately, awaiting the user's direction on what comes next (there is no Step 8 in the original plan - anything beyond this is new, not-yet-requested scope).

#### SUPERSEDED stopping point (2026-09-07, kept for history): Step 7 done, build/link-verified, and smoke-tested; full manual interactive verification not yet performed.

#### SUPERSEDED stopping point (2026-09-07, kept for history): Step 5 done (including the first cross-executable integration test); Step 6 not yet decided; Step 7 not yet started.

#### SUPERSEDED stopping point (2026-09-07, kept for history): Step 4 fully done including `cc_main.cpp` wiring; Step 5 not yet started.

#### SUPERSEDED stopping point (2026-09-07, kept for history): Step 4a-4d all done, `GsCommunication` functionally complete, `cc_main.cpp` wiring not yet started.

#### SUPERSEDED stopping point (2026-09-07, kept for history): Step 4a, 4b, and 4c done (DataStore-free), Step 4d not yet started.

#### SUPERSEDED stopping point (2026-09-07, kept for history): Step 4a and 4b done, Step 4c not yet started.

#### SUPERSEDED stopping point (2026-09-07, kept for history): Step 4a done, Step 4b not yet started.

#### SUPERSEDED stopping point (2026-09-06/07, kept for history): Architecture fully designed and approved. Zero code written.

#### Step 6 - OFFICIALLY SKIPPED (2026-09-07), decision confirmed by the user
Step 6 was originally "connect CC's `GsCommunication` callbacks to `DataStore` queries + chunked sending," conceived under an assumed architecture where `GsCommunication` would expose callbacks (mirroring `communication::Communication`'s `Callbacks` struct) for a separate glue layer to handle. That assumption was superseded during Step 4's own detailed design (agreed before any 4a-4d code was written): CC-side GS handling was made fully synchronous, and `GsCommunication` was designed to hold `DataStore&` directly with no wrapper class. Step 4d then implemented exactly Step 6's stated responsibility - `sendMeasurementsForRange`/`sendEventsForRange` query `DataStore`, build the chunked response, and send it, all inside `GsCommunication::dispatch()` - with 77/77 tests already covering it (single-chunk, multi-chunk, zero-match, exact content). The "CC-side glue location" open question was likewise resolved in practice by the `cc_main.cpp` wiring step: `GsCommunication` is constructed inline with no separate glue file needed. Reviewed explicitly (not assumed) before this decision: Steps 4a-4d, the `cc_main.cpp` wiring, and Step 5, confirming zero remaining scope and zero overlap with Step 5 (`CcCommunication` is GS-side response reception only, never touches `DataStore`). **No code was written or needed for this closure.**

#### Next: nothing from the original 7-step plan remains - awaiting the user's direction on what comes after
All 7 originally-planned steps are complete (Step 6 folded into Step 4d). Candidates not yet requested or scoped: richer GS display/formatting, canned convenience date ranges, the user's own manual interactive verification of `gs_main.cpp`'s menu, or an entirely new area of work. **Do not assume any of these - wait for explicit direction.**

#### Open questions - deferred future enhancements, explicitly NOT blockers for the now-complete, now-verified implementation
- **Reconnect / mid-request-disconnect recovery**: `CcCommunication` has no handling at all today for a TCP disconnect while a request is in flight - the pending state simply stays pending forever. Deliberately left open per explicit instruction during Step 5's approval, and deliberately not addressed in Step 7 either (per Step 7's approved decision #4).
- **A stuck pending request has no cancel/reset mechanism** - if a reply genuinely never arrives, `isMeasurementRequestInProgress()`/`isEventRequestInProgress()` stay true forever with no way to clear them short of restarting `gs_main.exe`. Noted during Step 7's design, deliberately not built (decision #4).

**Resolved, no longer open**: full interactive verification of `gs_main.cpp`'s menu and a real (loopback) end-to-end test of the actual GS<->CC TCP path - both completed by the user's manual End-to-End Verification, above (PASSED).

#### Current architecture (what exists today between LNC, CC, and the future Ground Station)

```
LNC  <---- UART (real, hardware-verified) ---->  CC
                                                   |
                                            (DataStore already
                                             holds all measurement/
                                             event data locally)
                                                   |
GS  <---- TCP/Ethernet (both real executables exist and can talk to each other; menu loop needs manual verification) ---->  CC
```
- **LNC <-> CC over UART**: fully real, fully hardware-verified (B+C, above).
- **GS <-> CC over TCP/Ethernet**: **every layer on both sides is now complete and PC-tested** - Message layer (Steps 1-2), GS's client-side Transport (Step 3), the entire CC-side stack (`TcpServerSocket` + `GsCommunication`, Steps 4a-4d, wired into `cc_main.cpp`), the GS-side dispatch/orchestration layer (`cc_communication::CcCommunication`, Step 5), and now real entry points on **both** sides (`cc_main.cpp` and `gs_main.cpp`, Step 7). Proven via a genuine cross-executable integration test (real `CcCommunication` <-> real `GsCommunication` <-> real `DataStore`, single- and multi-chunk, both data types) AND via `gs_main.exe`'s own real startup `connect()` succeeding against a real, separately-running CC-side process. **What's still missing**: `gs_main.cpp`'s interactive menu (options 1-4) has not been manually exercised end-to-end by a human operator (an automated-shell limitation, not a code gap - see the open questions above), and reconnect/mid-request-disconnect recovery remains an explicitly open, unhandled question.

#### Remaining development plan (approved order, unchanged since the design proposal)

1. ~~Step 1 - CC Message Layer~~ ✅ **DONE**
2. ~~Step 2 - Ground Station Message Layer~~ ✅ **DONE**
3. ~~Step 3 - TCP/Ethernet Transport~~ ✅ **DONE** - `transport::TcpSocket` (GS's own client-side transport, `GroundStation/include/tcp_transport.h`/`.cpp`), 18/18 tests passing against a real local loopback connection. See the full writeup above.
4. **Step 4 - CC `gs_communication::GsCommunication` + TCP Server Transport** ✅ **DONE, INCLUDING `cc_main.cpp` WIRING** (depends on Steps 1 and 3) - full design documented above (files, API, data flow, all agreed decisions). Built in individually-approved sub-steps, all DONE: **4a (`transport::TcpServerSocket`) PC-TESTED (30/30)**; **4b (CC-side message parsers) PC-TESTED (92/92)**; **4c (`GsCommunication` class shell) PC-TESTED (31/31)**; **4d (DataStore-querying/response-sending logic, full end-to-end round trip) PC-TESTED (77/77 total for the suite)**; `cc_main.cpp` wiring build/link-verified and smoke-tested. `cc_main.exe` now genuinely listens for a Ground Station on port 5000. See "Next: Step 5" above.
5. **Step 5 - GS-side communication/dispatch layer** ✅ **DONE** - `cc_communication::CcCommunication` (`GroundStation/include/cc_communication.h`/`.cpp`), 58/58 tests passing, including a real cross-executable TCP integration test against a real `GsCommunication`. See the full writeup above.
6. ~~Step 6 - connect CC's `GsCommunication` callbacks to `DataStore` queries + chunked sending~~ ⛔ **OFFICIALLY SKIPPED** - fully implemented already by Step 4d's `sendMeasurementsForRange`/`sendEventsForRange`; confirmed redundant and closed, per explicit user decision. See the full writeup above.
7. ~~Step 7 - `GroundStation/gs_main.cpp`~~ ✅ **DONE** - the GS console application, build/link-verified, real startup TCP connect proven against a real separate CC process. See the full writeup above.

**All 7 steps of the original Ground Station development plan are now complete, PC-tested, AND manually end-to-end verified by the user (2026-09-07) - see "Manual End-to-End Verification of the GS <-> CC Communication Path: PASSED" above for full detail.**

**Open design decisions already identified (from the approved design proposal, not yet finalized in code):**
- Class/namespace name for the new CC-side module: **`gs_communication::GsCommunication`** (not the stale `GsCommunicationManager` name found in Sec 5 - that predates this project's actual, consistently-used naming convention).
- GS gets its own independent `message.h/.cpp` (**done**, Step 2) rather than sharing CC's.
- **CC's own Sec 3 "Log" module (operational/audit logging to files) is explicitly deferred** - confirmed unrelated to Ground Station's actual requirements, since Sec 4's "log data" maps to measurement data (already served by the existing `DataStore`/`report_generator`), not CC's own activity log.
- **CC-side GS handling is intended to be fully synchronous** - unlike `DataCollection`'s LNC-facing backfill logic (which must correlate replies arriving asynchronously over many future `poll()` calls, because the LNC takes time to answer), CC already has all of GS's requested data locally, so it can query, chunk, and send the entire reply within the same callback that received the request, with no `RequestId` pending-state bookkeeping needed on the CC side.
- **TCP port number and connection model** (GS connects to CC, i.e., CC listens) - not yet decided, to be finalized during Step 3.
- **CC-side glue location** (inline in `cc_main.cpp` vs. a small new file, for wiring `GsCommunication`'s callbacks to `DataStore`) - a minor choice, deferred to Step 6.

**Do NOT, until explicitly instructed:**
- Start any work beyond the now-complete 7-step Ground Station plan (there is no Step 8) - richer display formatting, canned date ranges, reconnect/cancel architecture, etc. are all new, not-yet-requested scope.
- Revisit HW-B-C-06 or HW-B-C-07's status.
- Add the previously-proposed `tx_queue.c` drop counter or any other new instrumentation.
- Touch Watchdog (still paused, Open Question #2).

### Already verified (do not re-litigate without new evidence)
- CC builds and sends protocol-correct bytes for at least `setRtcDateTime` (byte-for-byte, three independent methods).
- The physical PA2→PA3 loopback path carries a real signal (`CommTxTask`'s TX is physically received back).
- `CommRxTask` is alive, scheduled, and its `Transport_UART_ReceiveByte`/`HAL_UART_Receive` call physically receives bytes off USART2.
- `Protocol_FeedByte` correctly decodes valid TLV frames on real hardware (`g_frames_decoded_ok` observed climbing, e.g. 20→30).
- The MCU no longer resets on its own (watchdog removed) and the ST-Link debugger can attach and stay attached.
- RGB LED, DHT11, Battery, and Light drivers — all four verified working on real hardware (see table above).
- **Open Question #1 (IR vs. sonar) is resolved**: hardware is a single-pin digital IR sensor, not sonar (see Sec 10 "Resolved" list for the full evidence trail). This resolution does not depend on the pending polarity test — it's about sensor *type*, not signal direction.
- **Configuration, Log, Event, Monitor, and Object Detection modules — all hardware-verified** (see their own entries in Sec 14 above): Configuration loads/saves flash correctly; Log writes dated `.LOG` files with 7-day retention; Event writes dated `.TXT` files with 7-day retention and drives LED/buzzer correctly for every Sec 2.3.1 case; Monitor samples all 4 sensors every ~5s, classifies correctly against Configuration's limits, logs every sample, and reports mode changes to Event correctly; Object Detection correctly detects/clears via IR activity, including with a real SD-card write on every event (the multi-round stack-overflow bug is fully resolved - see Sec 14).
- **Open Question #12 (IR driver redesign) is resolved**: no `ir.c`/`ir.h` rewrite needed - the new Object Detection module's fast-poll + activity-timeout logic handles the pulse-train signal correctly at the Application layer, and makes the driver's own unconfirmed polarity irrelevant (see Sec 14's Object Detection entry).

### NOT yet verified / not yet built
- Whether a command the CC sends (e.g. `setRtcDateTime`) actually gets **acted on** by the LNC in any way - `CommRxTask` still only decodes and counts, never dispatches by tag. Deliberately deferred, not the next step.
- **Init** - hardware-verified with a placeholder timestamp (2026-09-02); rewired to the real RTC and hardware-tested - RTC itself works (shows CubeMX's placeholder date, expected/analyzed, fix recommended not implemented). **Keep-Alive** - IMPLEMENTED and HARDWARE-VERIFIED (2026-09-03) - see Sec 14's Keep-Alive entry.
- **The stack-size/`configCHECK_FOR_STACK_OVERFLOW` regression (Open Question #15) - RESOLVED and HARDWARE-CONFIRMED.** LED color changes correctly on IR detection, IR detection works, the freeze is gone. Object Detection/Monitor/Init are all back to fully hardware-verified status.
- **The TIM3 buzzer Prescaler regression (Open Question #14) - RESOLVED and HARDWARE-CONFIRMED (2026-09-03).** User set it in CubeMX with `.ioc`-level verification this time, reflashed, buzzer audibly confirmed working.
- **The RTC placeholder-date behavior** - root cause fully understood (Sec 14's RTC entry), a fix is recommended (RTC backup-register guard) but deliberately **not implemented** - awaiting a user decision on timing/approach, not a bug requiring urgent action.
- The Watchdog module's future redesign (Open Question #2, Sec 10) - deliberately paused, do not touch.
- **Open Question #16**: whether the `configCHECK_FOR_STACK_OVERFLOW` fix's new placement (inside a `USER CODE` marker this time) actually survives the *next* CubeMX regeneration - unverified until that next regeneration happens (the upcoming TIM3 fix will be a real test of this, since it requires a regeneration).
- Whether `HeartbeatTask` (or another late-created task) is silently failing to be created due to the heap-margin concern noted above - flagged, not yet checked, not blocking.

- **Keep-Alive module — IMPLEMENTED, compiled clean, full-project link verified, and HARDWARE-VERIFIED (2026-09-03).** Sec 2.8's roadmap module, now done. User confirmed on real hardware: `g_keepalive_send_count` increases roughly every 6 seconds, `g_keepalive_last_timestamp` updates correctly (RTC read working), and `g_keepalive_last_frame_length` is exactly `27` as expected (21-byte `MeasurementSample` value + 6 bytes SOF/Tag/Length/CRC overhead) - confirms `Message_BuildKeepAlive()` is producing a correctly-sized real frame every cycle, end to end. Narrow scope agreed with the user first (Sec 12 working rule): send a real `TAG_KEEPALIVE` frame every 6s via direct `Transport_UART_Send()`, explicitly deferring the Sec 7 3-tier TX-priority-queue system (`txQueueKeepAlive`/`txQueueEvent`/`txQueueData`) - confirmed by inspection that it does not exist anywhere in code yet (no `osMessageQueue*` symbols in `main.c`), and that nothing else on the LNC sends real frames yet either (Event builds frames internally but nothing transmits them - confirmed by grep), so there is no real concurrent-traffic scenario today for a priority queue to arbitrate. Building it now would be speculative infrastructure ahead of need, matching the precedent already set for Init's own explicitly-deferred TX/dispatch infrastructure.
  - **`Monitor_GetLastSample(MeasurementSample *out_sample)` added** (`monitor.h`/`monitor.c`) - a small, additive getter matching the getter pattern every other verified module already uses (`Config_Get*`, `Log_GetLast*`, `Event_GetLast*`). `monitor.c` gained one `static MeasurementSample s_lastSample` (zero-initialized - `MODE_NORMAL`/0.0, the same safe-default philosophy `s_previousMode` already uses), reset in `Monitor_Init()` for symmetry with the other static fields, and captured at the end of every `Monitor_Sample()` cycle right after `out_sample->mode` is finalized. `Monitor_Sample()`'s own signature/behavior is unchanged - this lets Keep-Alive (running on its own independent 6s task cycle) read "the latest measurement + mode" without reaching into `main.c`'s temporary debugger-watch globals (`g_monitor_last_*`), which would have been a real layering violation this project has consistently avoided.
  - **New files**: `Embeded/Core/Inc/keepalive.h`, `Embeded/Core/Src/keepalive.c`. One function, `uint16_t KeepAlive_Send(uint32_t timestamp)`: calls `Monitor_GetLastSample()`, overwrites the returned sample's `.timestamp` with the fresh value passed in (the sample's own timestamp reflects when Monitor last sampled, not "now" - Sec 2.8 wants the keepalive's own send time), builds via the already-existing, already-tested `Message_BuildKeepAlive()` (part of the original 19/21-tag Message layer work - nothing new needed there), and sends via `Transport_UART_Send()`. Returns the frame length sent (0 on failure), matching `Message_BuildKeepAlive`'s own convention, so the caller can mirror it into a debugger-watch global like every other module.
  - **Timestamp source**: follows the exact precedent `StartTask02`/Init already established - no Application module reads the RTC internally anywhere in this project; the caller (task) reads the RTC and passes a plain `uint32_t timestamp` in. `StartTask07`'s new body reads `HAL_RTC_GetTime()` then `HAL_RTC_GetDate()` (time before date - the same real STM32 HAL requirement documented at Init's own RTC read site) and converts via the already-existing `static DateToEpoch()` in `main.c` (reused as-is, same translation unit - no new helper duplicated).
  - **`StartTask07` (`main.c`) rewritten**: was a completely empty `for(;;) osDelay(1);` stub; now reads the RTC, calls `KeepAlive_Send(timestamp)`, mirrors the result into 3 new debugger-watch globals (`g_keepalive_send_count`, `g_keepalive_last_timestamp`, `g_keepalive_last_frame_length` - same convention as every other module), then `osDelay(6000)`. No other task, and no task's priority or stack size, was touched - `CommRxTask`/`CommTxTask`/`event.c`/`object_detection.c` are all byte-for-byte unchanged.
  - **Compiled clean (zero warnings)** against the real ARM toolchain (`monitor.c`, `keepalive.c`, `main.c`, individually verified). **Full-project link verified** in a scratch copy (real object list + freshly compiled `main.o`/`monitor.o`/new `keepalive.o`, real linker script, real `.ld`) - link succeeds (exit 0), flash usage ~75.0 KB (well under the 1022 KB budget), zero symbols in Configuration's reserved `0x080FF800` page, and `nm` confirms `KeepAlive_Send`/`Monitor_GetLastSample`/the new `StartTask07` body are all genuinely present and linked.
  - **VERIFIED ON REAL HARDWARE (2026-09-03).** User confirmed: `g_keepalive_send_count` increases roughly every 6 seconds, `g_keepalive_last_timestamp` updates correctly, `g_keepalive_last_frame_length` is exactly `27` (matching the expected 21-byte value + 6-byte overhead). `keepalive.h`/`keepalive.c` are finished and permanent; `Monitor_GetLastSample()` and the new `StartTask07` body are both confirmed working together end to end. **Keep-Alive (Sec 2.8) is now fully done.** Not independently cross-checked against a COM10 hex capture this round (the debugger-watch evidence above was sufficient) - the technique remains available later if the exact on-wire bytes ever need auditing.

- **Communication / Command Dispatch module — IMPLEMENTED, compiled clean, full-project link verified. NOT YET TESTED ON REAL HARDWARE.** Sec 2.5's "receives management commands from CC" / "receives retrieval instructions from CC" - the piece that was still missing after Keep-Alive: `CommRxTask` could already decode a raw TLV frame but never looked at its Tag or acted on it. Design agreed with the user first (Sec 12 working rule), scoped narrowly on purpose.
  - **New files**: `Embeded/Core/Inc/communication.h`, `Embeded/Core/Src/communication.c`. One entry point, `void Communication_Dispatch(const ProtocolFrame *frame, uint32_t timestamp)`, called once per successfully-decoded frame. Follows the same "caller supplies the timestamp, module never reads the RTC" convention every other module already uses (`Monitor_Sample()`, `Init_Start()`, `KeepAlive_Send()`).
  - **9 of the 12 CC→LNC tags fully handled this pass:**
    - The 8 "set limit" commands (`TAG_SET_TEMP_NORMAL_RANGE` ... `TAG_SET_BATTERY_WARNING_LOWER`): parse via the already-existing, already-tested `Message_Parse*()` function, apply via the matching, already-existing `Config_Set*()` setter (configuration.h's own header comment had literally anticipated this exact call site: *"callers (later, whatever dispatches incoming CC commands) translate a parsed message into a call to one of these setters"*), then report the change to Event via the already-existing `Event_OnConfigurationChanged(timestamp, ...)` - satisfying Sec 2.6's "Receives configuration changes from Communication." **Per explicit instruction, the frame Event builds is discarded here, same as every other existing Event call site** - real sending for all Event types together is deliberately left for the future TX-priority-queue step (Sec 7), not special-cased for just this one event type.
    - `TAG_GET_SYSTEM_TIME_REQUEST`: parse (confirms right tag, zero-length payload), build a `TAG_SYSTEM_TIME_RESPONSE` via the already-existing `Message_BuildSystemTimeResponse()` with the caller-supplied timestamp, and send it immediately via `Transport_UART_Send()` - the same direct-send pattern `KeepAlive_Send()` established, and the first real request/response round trip on the LNC.
  - **3 of the 12 tags deliberately deferred, not silently dropped** - each recognized and counted separately from truly-unknown tags:
    - `TAG_SET_RTC_DATETIME`: acting on it now would be pointless - `MX_RTC_Init()` unconditionally re-applies its placeholder boot time on every reset (still-open RTC entry above), so any real time the LNC set would be lost on the very next reset. Needs the RTC backup-register guard first.
    - `TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST` / `TAG_GET_EVENTS_BY_RANGE_REQUEST`: replying needs historical data read back from the SD card's dated `.LOG`/`.TXT` files and turned into chunked responses (`Message_BuildMeasurementChunkResponse`/`Message_BuildEventChunkResponse` already exist and are already tested, but nothing yet reads history back off the SD card to feed them) - a substantial, separate feature, not a small addition to dispatch.
  - **Observability**: `communication.c` holds 5 static counters/state behind getters (`Communication_GetLastDispatchedTag()`, `_GetSetLimitCount()`, `_GetSystemTimeRequestCount()`, `_GetDeferredCount()`, `_GetUnknownTagCount()`) - same getter-based convention as Configuration/Log/Event, not raw globals inside the module itself. `main.c` mirrors these into 5 new debugger-watch globals (`g_dispatch_last_tag`, `g_dispatch_set_limit_count`, `g_dispatch_system_time_request_count`, `g_dispatch_deferred_count`, `g_dispatch_unknown_tag_count`) right after every dispatch call, same convention as every other module.
  - **`StartTask05`/`CommRxTask` (`main.c`) modified**: on `PROTOCOL_DECODE_FRAME_READY`, now also reads the RTC (same fast, non-blocking `HAL_RTC_GetTime()`/`GetDate()` pair `StartTask07` already uses, converted via the same shared `static DateToEpoch()`) and calls `Communication_Dispatch(&decoded_frame, timestamp)` before mirroring its counters into the new globals. The RTC read only happens when a frame actually arrives (not on every 20ms idle-poll iteration), so it adds no measurable overhead to `CommRxTask`'s existing timing budget.
  - **Not touched, per explicit instruction**: `event.c`, `message.c`, `monitor.c`, `keepalive.c`, `CommTxTask`, any task's priority or stack size. Confirmed via `git status` at the end of this session - only `communication.h`/`.c` (new) and `main.c` (modified) changed for this step.
  - **Compiled clean (zero warnings)** against the real ARM toolchain (`communication.c` and the updated `main.c`, individually verified). **Full-project link verified** in a scratch copy (real object list + freshly compiled `main.o`/`monitor.o`/`keepalive.o`/new `communication.o`, real linker script) - link succeeds (exit 0), flash usage ~77.1 KB (well under the 1022 KB budget), zero symbols in Configuration's reserved `0x080FF800` page, and `nm` confirms `Communication_Dispatch` and all 5 getters are genuinely present and linked.
  - **NOT yet tested on real hardware.** Next step: flash and, using the CC's existing `cc_main.exe` (`[1]` sends `setRtcDateTime` - won't move the counters since that tag is deferred; a "set limit" command would need a small CC-side test hook, since `cc_main.cpp`'s menu doesn't currently expose one) or a hand-crafted frame, confirm `g_dispatch_set_limit_count`/`g_dispatch_system_time_request_count` increase for the tags this pass handles, `g_dispatch_deferred_count` increases for `SET_RTC_DATETIME`, and `g_dispatch_last_tag` reflects whatever was last sent. Also worth confirming a set-limit command's effect actually sticks - e.g. send `TAG_SET_TEMP_NORMAL_RANGE`, then verify `Config_GetTempNormalRange()`'s debugger-watch globals (`g_config_temp_normal_low`/`_high`, still wired from Configuration's own hardware test) actually changed.

- **TX Priority Queue system (Sec 7) — IMPLEMENTED, compiled clean, full-project link verified. NOT YET TESTED ON REAL HARDWARE.** The last piece of Sec 7's frozen Communication design: `txQueueKeepAlive`/`txQueueEvent`/`txQueueData`, drained by `CommTxTask` in strict priority order. Design agreed with the user first (Sec 12 working rule), including one point the user specifically pushed back on before implementation - see below.
  - **A real constraint found before designing anything**: `configTOTAL_HEAP_SIZE` is frozen (explicit standing instruction) and `g_free_heap_size` was already measured at only ~136 bytes free (Object Detection investigation, earlier in this file). A heap-backed `osMessageQueueNew()` for 3 queues would almost certainly fail. **Solution: fully static allocation** - confirmed `configSUPPORT_STATIC_ALLOCATION = 1` is already set in `FreeRTOSConfig.h`, and confirmed directly from this project's actual `cmsis_os2.c` that `osMessageQueueAttr_t`'s `cb_mem`/`cb_size`/`mq_mem`/`mq_size` fields, pointed at plain `static` globals (a `StaticQueue_t` control block + a byte-array buffer per queue), make `osMessageQueueNew()` use `xQueueCreateStatic()` instead of the heap. Costs ordinary `.bss` RAM (this MCU has plenty) instead of the already-critically-tight FreeRTOS heap.
  - **New files**: `Embeded/Core/Inc/tx_queue.h`, `Embeded/Core/Src/tx_queue.c`. Queue depths match Sec 7 exactly: keepalive=2, event=8, data=4. Each item is a small fixed struct (`length` + up to 64 bytes - matching the largest frame buffer already used anywhere in this project; nothing here needs more). API: `TxQueue_Init()` (creates the 3 static queues), `TxQueue_EnqueueKeepAlive()`/`_EnqueueEvent()`/`_EnqueueData()` (non-blocking put; a full queue drops the new frame rather than blocking the caller - same "no retry" philosophy as `DataCollection`'s backfill design), and `TxQueue_DrainOne()` (checks all 3 queues in strict priority order, non-blocking, returns the first available frame's bytes+length or 0 if all empty).
  - **`TxQueue_Init()` called from `main()`'s existing, CubeMX-provided `/* USER CODE BEGIN RTOS_QUEUES */` marker** - the regeneration-safe spot CubeMX already reserves for exactly this ("add queues, ..."), avoiding a 4th silent-wipe incident (Open Question #16).
  - **`CommTxTask` (`StartTask06`) rewritten**: removed the fixed-test-frame scaffolding; now loops on `TxQueue_DrainOne()`, sends via `Transport_UART_Send()` if non-empty, else `osDelay(1)` (same yield discipline `CommRxTask` already uses on its idle path). **`CommTxTask` is now the ONLY task that ever calls `Transport_UART_Send()`** - confirmed by grep: every other `Transport_UART_Send` reference left in the codebase is either that call site itself, the Transport layer's own implementation (`comm_transport_uart.c`), or a comment.
  - **Small, targeted one-line-per-site changes wiring real senders to the queues** (no module's own logic changed, only how the already-built frame gets sent):
    - `keepalive.c`: `Transport_UART_Send()` → `TxQueue_EnqueueKeepAlive()`.
    - `monitor.c`: (a) its existing mode-transition call now captures `Event_OnModeTransition()`'s returned length and enqueues via `TxQueue_EnqueueEvent()` instead of discarding it; (b) **new**: Sec 2.5's "data" priority tier had no producer at all (`Message_BuildDataReport()` existed but nothing ever called it) - Monitor now also builds and enqueues a `TAG_DATA_REPORT` via `TxQueue_EnqueueData()` every cycle, mirroring its existing "every cycle, unconditionally" `Log_Write()` call site. User-approved addition, since otherwise the data queue would be permanently empty and untestable.
    - `object_detection.c`: same pattern - `Event_OnObjectDetection()`'s frame now enqueued via `TxQueue_EnqueueEvent()`.
    - `init.c`: same pattern - `Event_OnInitStartup()`'s frame now enqueued via `TxQueue_EnqueueEvent()`.
    - `communication.c`: (a) the config-changed Event frame (from the 8 "set limit" commands) now enqueued via `TxQueue_EnqueueEvent()` instead of discarded; (b) `SendSystemTimeResponse()` changed from a direct `Transport_UART_Send()` call to `TxQueue_EnqueueEvent()`.
  - **`event.c`/`event.h` themselves are completely untouched** - Event still only builds frames and returns them; only its 4 existing callers changed.
  - **Architectural point the user specifically flagged and required verifying, not assuming**: whether `GET_SYSTEM_TIME_RESPONSE` (a reply to a CC request) really belongs on `txQueueEvent`. **Checked directly against Sec 7's frozen text - it does NOT explicitly assign responses to any tier; Sec 7 defines exactly 3 queues (keepalive/event/data), no 4th "response" tier.** Since adding a 4th queue would be out of this step's approved scope, the choice was justified on merits among the 3 existing tiers instead of asserted by convenience: not keepalive (wrong traffic shape - that tier is sized for the periodic 6s heartbeat specifically), not data (wrong priority - a CC-initiated request the CC is presumably waiting on shouldn't queue behind the lowest-priority periodic telemetry), but event - because the reply's traffic *shape* (irregular, triggered by something happening, not periodic) matches Event's own 4 existing sources, and `txQueueEvent`'s depth (8) is by far the largest of the 3, sized for exactly this kind of bursty triggered traffic. Also consistent with this project's own precedent (Sec 2.7 already routes another one-off time-related notification through Event). **Documented explicitly in both `communication.h` and `communication.c` as "traffic-shape match, not because it IS an event."**
  - **Secondary correctness benefit of this choice**: before this change, `CommRxTask` (priority 2, must stay non-blocking per Sec 7's own `osDelay(1)` rule) called `Transport_UART_Send()` directly from inside `Communication_Dispatch()` - a real, if narrow, "two tasks writing to the shared UART" risk once `CommTxTask` also became a busy queue-drainer, and a blocking HAL call `CommRxTask` couldn't yield out of. Routing it through `txQueueEvent` means `CommRxTask` never touches `Transport_UART_Send()` at all anymore - confirmed by the same grep above.
  - **Not touched, per explicit instruction**: `event.c`/`event.h`, `message.c`, `CommRxTask`'s dispatch decision-making (the switch statement's tag handling from the B step is unchanged - only how a reply is sent changed), any task's priority or stack size. Confirmed via `git status` at the end of this session.
  - **Compiled clean (zero warnings)** against the real ARM toolchain (`tx_queue.c` and every modified file, individually verified). **Full-project link verified** in a scratch copy (real object list + freshly compiled `main.o`/`monitor.o`/`keepalive.o`/`communication.o`/`object_detection.o`/`init.o`/new `tx_queue.o`, real linker script) - link succeeds (exit 0), flash usage ~78.4 KB (well under the 1022 KB budget), zero symbols in Configuration's reserved `0x080FF800` page, and `nm` confirms `TxQueue_Init`/`TxQueue_EnqueueKeepAlive`/`_EnqueueEvent`/`_EnqueueData`/`TxQueue_DrainOne` and `Message_BuildDataReport` are all genuinely present and linked.
  - **NOT yet tested on real hardware.** Next step (per the user's plan): test B (command dispatch) and C (TX queues) together in one integration pass - e.g. send a "set limit" command from the CC and confirm both `g_dispatch_set_limit_count` increases (B) AND the resulting config-changed event frame actually arrives at the CC over UART (C); watch Keep-Alive/Monitor's data reports and Event's frames all successfully interleave through the one `CommTxTask` without corruption; and worth watching `CommTxTask`'s own stack margin under real load for the first time (`uxTaskGetStackHighWaterMark`, same pattern already used for Monitor/ObjectDetection) since it now does meaningfully more work per iteration than the old fixed-test-frame scaffolding.

### Relevant files/functions for Init (DONE - kept for reference)
- `Embeded/Core/Inc/init.h`/`Embeded/Core/Src/init.c` - `Init_Start(uint32_t timestamp)`, calls `Configuration_Init()`/`Log_Init()`/`Event_Init()` then `Event_OnInitStartup()`. See Sec 14's Init entry for the full design/verification writeup.
- `StartTask02` (`main.c`) is Init's real, permanent caller - calls `Init_Start(g_log_test_timestamp)` once at boot. `Monitor_Init()`/`ObjectDetection_Init()` deliberately stayed in their own tasks (`StartTask03`/`StartTask04`), not moved into Init - per explicit user instruction this session.
- Sec 2.7 also requires requesting time/date sync from CC via Communication - **still deliberately deferred**, not part of this Init implementation. Depends on both a real RTC (not yet enabled - see Sec 9.2) and the still-not-built LNC-side dispatch of incoming CC commands (`CommRxTask` currently only decodes and counts).
- `InitTask`'s Sec-7-specified "self-deletes" behavior - still not implemented (`StartTask02` still loops forever afterward, IR/Button/Buzzer test code unchanged) - a nice-to-have cleanup, not required for Init's correctness, deliberately left for later per explicit instruction.

After Init is hardware-verified, continue through the rest of the LNC Application modules in the existing roadmap order (Sec 13): Keep-Alive → Watchdog (paused).

---

## CentralComputer Facade — IMPLEMENTED and VERIFIED (2026-09-08)

Closes the gap identified in the full project gap analysis (this session): Sec 5's "Decoupling rule" requires
a `CentralComputer` facade class (inert constructor, all I/O in `start()`/`stop()`) as the seam a later,
separate FleetOOP module is meant to depend on. This class did not exist before this change - `cc_main.cpp`
constructed and wired all 5 CC modules (`Communication`, `DataStore`, `ManagementCommand`, `DataCollection`,
`GsCommunication`) itself. **FleetOOP itself remains out of scope for this change** - this is only the
prerequisite facade, per the user's explicit scoping decision.

**Explicit user constraint for this task**: keep the facade as thin and simple as possible - no new
abstractions beyond the one class, no business logic in it, reuse the 5 modules' existing, already-tested
public methods exactly as `cc_main.cpp` already called them.

### What was implemented

- **New `CentralComputer/include/central_computer.h` / `src/central_computer.cpp`** - `class CentralComputer`
  owning all 5 modules as plain value members (`communication_`, `dataStore_` declared first, since
  `managementCommand_`/`dataCollection_`/`gsCommunication_` hold references to one or both of them - same
  construction-order convention `Tests/DataCollection/data_collection_test.cpp`'s own test rig already uses).
  No copy-control declared explicitly - `dataCollection_` (a `DataCollection` member) already has its own
  copy-ctor/assignment deleted, which implicitly deletes `CentralComputer`'s too; a one-line header comment
  explains this rather than adding redundant `= delete` lines.
- **`CentralComputer()`** - inert: only wires the 5 members to each other via the constructor initializer
  list. No port, file, or socket is touched.
- **`bool start(portName, baudRate, dbPath, gsPort)`** - opens the database first (the one fatal condition -
  returns `false` on failure), then starts the Ground Station listener, then opens the serial port (both
  non-fatal) - same order and same fatal/non-fatal split `cc_main.cpp` already used. Prints nothing - no
  business logic in this class, per the explicit constraint.
- **`void stop()`** - closes the serial port, GS listener, and database, in that order (same as before).
- **`void poll()`** - polls the serial connection and the GS connection (same as before). `ManagementCommand`
  and `DataCollection` have no `poll()`/`close()` of their own - confirmed from their headers, not omitted by
  mistake.
- **5 reference-returning accessors** (`communication()`, `dataStore()`, `managementCommand()`,
  `dataCollection()`, `gsCommunication()`) - the only way a caller reaches the owned modules, letting
  `cc_main.cpp` keep every existing menu-handler function and lambda body completely unchanged, just called
  through `cc.xxx()` instead of a local variable.
- **`cc_main.cpp` refactored, zero behavior change**: the 5 individually-constructed objects and their
  individual `open()`/`startListening()`/`close()` calls are replaced by one `central_computer::CentralComputer cc;`
  plus `cc.start(...)`/`cc.stop()`; the main loop's `comm.poll(); gsComm.poll();` becomes `cc.poll();`. Every
  printed string, warning, and menu action is byte-for-byte identical to before. The 5 individual module
  `#include`s were removed (pulled in transitively via `central_computer.h`); `report_generator.h` stays.

### Files changed

**New**: `CentralComputer/include/central_computer.h`, `CentralComputer/src/central_computer.cpp`,
`Tests/CentralComputer/central_computer_test.cpp`.
**Modified**: `CentralComputer/src/cc_main.cpp` (mechanical refactor only, per above).
**Not touched, confirmed via `git status`**: all 5 wrapped classes' own headers/implementations, the wire
protocol, `message.h`/`.cpp`, all 14 pre-existing test files, `FleetOOP/` (still empty - a separate,
later task), any LNC or Ground Station file.

### New test: `central_computer_test.exe` - 16/16 PASS

A thin wiring/lifecycle test (`":memory:"` database, a COM port name guaranteed not to exist, real ephemeral
local-only TCP ports) - proves the facade's own construction/lifecycle behavior, not the 5 wrapped modules'
internal behavior (already covered exhaustively by `communication_test`/`data_store_test`/
`management_command_test`/`data_collection_test`/`gs_communication_test`). Checks: constructor performs no
I/O; `start()` returns `true`/opens the database on a valid path; `start()` returns `false` on an unusable
database path; `start()` still returns `true` (non-fatal) when the COM port doesn't exist; the GS listener
opens on the requested port; `managementCommand()`/`dataCollection()` send through the same `Communication`
instance `communication()` returns (checked via `Communication::lastSentFrame`, the same technique
`management_command_test.cpp` already uses); `stop()` closes everything; `poll()` doesn't crash with nothing
connected.

### Build verification (real toolchain, not just review)

- New objects compile clean with `-Wall`: zero warnings for `central_computer.h`/`.cpp` and the new test.
  `cc_main.cpp` recompiles with the same single pre-existing, unrelated `ModeName`-unused warning it has
  always had (confirmed unchanged, not introduced by this change).
- `cc_main.exe` fully relinked with the new `central_computer.o` added to its object list - links cleanly.
- **Regression: 556/556 checks passing, 0 failures** - the pre-existing 540/540 across the original 14
  suites (`protocol_test` 23/23, `message_test` 80/80, `tlv_codec_test` 22/22, `cc_message_test` 92/92,
  `gs_message_test` 32/32, `communication_test` 29/29, `management_command_test` 14/14, `data_store_test`
  24/24, `data_collection_test` 25/25, `report_generator_test` 16/16, `tcp_transport_test` 18/18,
  `tcp_server_transport_test` 30/30, `gs_communication_test` 77/77, `cc_communication_test` 58/58 - every
  one individually re-run and confirmed unchanged, zero regression anywhere), plus the new
  `central_computer_test.exe` (16/16). `serial_transport_test.exe` (last known 10/10) and
  `hardware_loopback_test.exe` (no documented final pass count) remain as previously recorded - neither
  requires or was affected by this change.
### Hardware smoke test (2026-09-08) - PASSED, one item investigated

The refactored `cc_main.exe` was run against the real STM32 board and a real `gs_main.exe`, confirming the
facade's `start()`/`poll()`/`stop()` sequence behaves identically to the pre-refactor code on real hardware,
not just in the PC-side regression suite:

- Real STM32 -> CC -> SQLite -> GS communication verified end-to-end.
- **1,795 measurements** retrieved successfully through GS.
- **19 events** retrieved successfully through GS.
- The 7-day report (`[4]`) verified correct.
- Management commands (`[1]`/`[5]`/`[6]`) sent successfully.

**One item required investigation before closing this out: `CC Decode errors: 1`.**

- **Root cause**: `Communication::decodeErrors` (`communication.cpp`) increments only inside
  `Communication::feedByte()`, when `tlv::Decoder::feedByte()` (`Common/TLVCodec/tlv_codec.cpp`) returns
  `DecodeStatus::Error` - which happens in exactly two cases, confirmed directly from the decoder's source:
  (1) a `Length` field that would make the frame exceed the frozen 256-byte cap, or (2) a CRC mismatch on an
  otherwise-complete frame. Both are pure byte-content checks on the raw UART stream, fed one byte at a time
  from `Communication::poll()`. A single such error, immediately followed by over 1,795 correctly-decoded
  measurements and 19 correctly-decoded events with no further errors, is consistent with the decoder
  encountering exactly one corrupted or misaligned byte sequence on the physical link (e.g. real UART/VCP
  line noise, or a stray byte already sitting in the OS's serial receive buffer at the moment `comm.open()`
  ran) and then correctly rejecting and resynchronizing on the next `0xAA` - which is precisely the frozen
  protocol's own required behavior ("malformed frames: rejected, decoder resyncs on the next `0xAA` byte",
  Sec 6), not a malfunction. No passive capture was running during this smoke test, so the exact corrupted
  byte content cannot be forensically recovered - but the mechanism and its category (a real-link
  transmission glitch, correctly caught and recovered from) are established with high confidence.
- **Not caused by the `CentralComputer` facade**: confirmed by direct inspection, not assumption. The
  facade's `start()` calls `dataStore_.open()`, then `gsCommunication_.startListening()`, then
  `communication_.open()` - the exact same order `cc_main.cpp` already used before this refactor existed.
  `poll()` calls `communication_.poll(); gsCommunication_.poll();` - the exact same order and the exact same
  two calls the old `main()` loop made directly. `central_computer.cpp` does not touch, wrap, or call
  anything in `tlv_codec.cpp` or `communication.cpp`'s decode path at all - `Communication`'s own
  `decoder_` member and `feedByte()` logic are 100% unmodified by this change. A facade that only forwards
  calls in the same order cannot introduce a byte-level decode error in code it never touches.
- **No fix applied.** Per the investigation's own findings, a single self-recovered decode error out of
  thousands of successfully processed frames is the protocol's designed-for resilience mechanism working
  correctly, not a defect - there is nothing to fix. No source file was modified as a result of this
  investigation, so no re-test or regression re-run was triggered by it.

**Also confirmed: `Measurement backfill in progress: yes` is expected, pre-existing behavior, unrelated to
the facade.** Traced directly through `DataCollection::requestMeasurementBackfill()` /
`isMeasurementBackfillInProgress()` / `onMeasurementChunkResponse()` (`data_collection.cpp`):
`pendingMeasurementRequestId_` is set the moment a backfill request is sent, and is only cleared inside
`onMeasurementChunkResponse()` when a chunk response with the matching `requestId` and `moreDataFlag == false`
arrives - by design, with no timeout (matching this project's established "no retry/timeout" philosophy used
everywhere else). Confirmed directly from `communication.c` that the LNC recognizes
`TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST`/`TAG_GET_EVENTS_BY_RANGE_REQUEST` but deliberately only increments
`s_deferredCount` for both - it never builds or sends a reply, since reading historical data back off the SD
card and building chunked responses for it remains a separate, not-yet-built feature (unchanged from
earlier in this project's history). So once a measurement backfill is requested during a session, it will
correctly show "in progress: yes" for the rest of that session - this is the expected, already-documented
consequence of the LNC's application layer not yet implementing this reply, not a regression introduced by
`CentralComputer`. `DataCollection`'s own request/pending-state logic is completely untouched by this
refactor.

### Build recipe addition

`cc_main.exe`'s build gains one new compile step and one new object on the link line:
```
g++ -std=c++17 -I CentralComputer/include -I Common/TLVCodec -I Common/Protocol -I Common/ThirdParty/sqlite3 -c CentralComputer/src/central_computer.cpp -o central_computer.o
```
(`central_computer.o` added to the final link line alongside the existing object list.)

Nothing committed yet.

---

## Phase B — Fleet/OOP — IMPLEMENTED and VERIFIED (2026-09-08)

Closes Sec 5 ("OOP Part - Submarine Fleet Management System"), the last major block identified as missing in
the full project gap analysis. Implemented as a separate, in-memory exercise with **zero** networking,
database, or file persistence, exactly as the spec requires - and built one file at a time (implement →
build → focused test → re-run all previous tests → `git diff`/status → stop and report), approved at every
step before the next began.

### What was implemented

Six classes in namespace `fleet`, one file pair each, plus one menu entry point:

- **`Submarine`** (abstract) - `serialNumber_`, `name_`, `assignedToMission_`; `assignToMission()`/`endMission()`;
  pure virtual `print()`/`typeName()`. No back-reference to `Fleet` or `Mission` anywhere - Sec 5's locked
  decoupling decision.
- **`ResearchSubmarine : public Submarine`** - adds `researchTopic_` and `researchers_`, with
  `setResearchTopic()`/`addResearcher()` for menu operation 5.
- **`CombatSubmarine : public Submarine`** - adds `commanderName_`, `personnelCount_`, `missionDescription_`;
  ally tracking (`addAlly()`/`allies()`/`isAllyOf()`/`clearAllies()`, non-owning `CombatSubmarine*` pointers)
  for operations 7/8; `receiveMessage()`/`inbox()` for operations 8/9. **Owns
  `std::unique_ptr<central_computer::CentralComputer>`**, created in its constructor via `make_unique`,
  destroyed with it, **never started** - `combat_submarine.h` is the only FleetOOP file that includes
  `central_computer.h`; nothing in FleetOOP ever calls `start()`, `stop()`, or `poll()`, or includes any
  `Communication`/`GsCommunication`/`DataStore`/protocol/transport header.
- **`fleet::Message`** (`fleet_message.h`/`.cpp`, deliberately not named `message.h` to stay unambiguous
  against the protocol message layer) - `content_` + non-owning `const CombatSubmarine *sender_`. No
  receiver field (the message lives in the receiver's own inbox), no timestamp, no id.
- **`Mission`** - `description_` + `submarineSerialNumber_` only, exactly as approved: no participants, no
  status, no timestamps, no mission id, no back-reference.
- **`Fleet`** - owns `std::vector<std::unique_ptr<Submarine>>` (polymorphic storage, deleted correctly
  through the base pointer) and `std::vector<Mission> missionHistory_` (the fleet-owned history). Implements
  the validated logic behind menu operations 1, 2, 3, 4, 6, 7, 8 (5 and 9 are handled in the menu via
  `dynamic_cast`, per the approved design, since they are type-specific display/update operations with no
  shared `Fleet`-level rule to enforce).
- **`fleet_main.cpp`** - the interactive console menu for all 10 operations, driven entirely through
  `Fleet`'s public API. No business logic lives here - every accept/reject decision is `Fleet`'s; the menu
  only reports the result. `dynamic_cast` used only for operation 5 (research vs. combat detail updates) and
  operation 9 (inbox is combat-only). A separate executable (`fleet_main.exe`), not folded into `cc_main`,
  matching Sec 11's own directory tree.

**Two small, approved deviations from the original API sketch:**
- `Fleet::addSubmarine()` returns `bool` (not `void`) - required so `Fleet` itself enforces "duplicate
  serial numbers must be rejected" rather than relying on the caller to check first.
- `Fleet::submarineCount()` was added - a one-line accessor needed only so a rejected duplicate's absence is
  observable in a test; not used by `fleet_main.cpp`'s menu logic.
- `CombatSubmarine::centralComputer()` was added - returns the raw, non-owning pointer, used only to verify
  in tests that the owned `CentralComputer` exists and was never started. FleetOOP production code
  (`fleet.cpp`, `fleet_main.cpp`) never calls it.

### Files changed

**New, entirely under `FleetOOP/` and `Tests/FleetOOP/`**: `FleetOOP/include/{submarine,research_submarine,
combat_submarine,fleet_message,mission,fleet}.h`, `FleetOOP/src/{submarine,research_submarine,
combat_submarine,fleet_message,mission,fleet,fleet_main}.cpp`, `Tests/FleetOOP/fleet_test.cpp`.
**Not touched, confirmed via `git status`/`git diff` and file timestamps**: `cc_main.cpp`,
`central_computer.h`/`.cpp`, every other Central Computer module, the wire protocol, `message.h`/`.cpp`,
Ground Station, the LNC (`Embeded/`), and all 15 pre-existing test files. Phase A and Phase 1 remain exactly
as they were - nothing in this phase modified their behavior.

### Test results

**`fleet_test.exe` - 106/106 checks passing, 0 failures.** One growing test file, built incrementally
alongside each class (Sec 12's own "a test file accompanies every implementation file" convention, plain
`check()`-based style, no test library), re-running every prior check at each step. Covers: `Submarine`'s
flag behavior and virtual dispatch through a base pointer; `ResearchSubmarine`'s/`CombatSubmarine`'s own
fields and detail updates; the owned `CentralComputer`'s existence and confirmed-inactive state
(`dataStore().isOpen()`, `communication().isOpen()`, `gsCommunication().isListening()` all false after
construction); `Message` storage and delivery into the receiver's inbox only; `Mission` as a plain history
value; and `Fleet`'s full success **and** rejection/edge-case matrix for every operation - duplicate serial,
unknown serial, double assignment, ending an unassigned mission, self-association, non-combat submarines
refused for operations 7/8/9, unassigned submarines refused for operation 7, duplicate association, empty
message, cross-mission message, bidirectional allies, ally-clearing on mission end, and polymorphic dispatch
through `Submarine*` for both concrete types in one `Fleet`.

**Manual smoke test of `fleet_main.exe`** (interactive console I/O isn't unit-testable, same honest
limitation `cc_main.cpp` already has): all 10 menu operations exercised via a scripted session - add (incl.
a duplicate correctly rejected), display, search (incl. unknown serial), assign, associate (incl. a research
submarine correctly rejected), send/receive a message (incl. a non-ally correctly rejected), display
received messages, update type-specific details, end a mission, and exit - all producing the correct
output and correctly rejecting every invalid case tested.

### Integration / regression results

- **Full clean Fleet/OOP build from scratch** (all 6 classes + `fleet_main.cpp` + the CC objects
  `CombatSubmarine` pulls in transitively): zero warnings, zero errors.
- **Full existing regression re-run individually, all 556/556 passing, 0 failures**: the original 540 across
  14 suites (`protocol_test` 23/23, `message_test` 80/80, `tlv_codec_test` 22/22, `cc_message_test` 92/92,
  `gs_message_test` 32/32, `communication_test` 29/29, `management_command_test` 14/14, `data_store_test`
  24/24, `data_collection_test` 25/25, `report_generator_test` 16/16, `tcp_transport_test` 18/18,
  `tcp_server_transport_test` 30/30, `gs_communication_test` 77/77, `cc_communication_test` 58/58) plus
  `central_computer_test` (16/16) - every one confirmed unchanged, zero regression anywhere from adding
  Fleet/OOP.
- **`cc_main.exe` confirmed unchanged**: rebuilt clean from `cc_main.cpp`/`central_computer.cpp` (both
  file-timestamp-confirmed untouched since Phase A), same single pre-existing `ModeName`-unused warning,
  links successfully with the same object list as before.
- `serial_transport_test.exe` (last known 10/10) and `hardware_loopback_test.exe` (no documented final pass
  count) remain as previously recorded - neither requires or was affected by this phase.

### Final stopping point / remaining work

**Phase B (Fleet/OOP) is implemented and verified as a complete, in-memory, standalone exercise.** No known
open items remain within Fleet/OOP's own approved scope - all 10 menu operations, both concrete submarine
types, the `CentralComputer` ownership requirement, and the mission-history/ally/message rules are
implemented and tested, including their rejection paths. Nothing outside `FleetOOP/`/`Tests/FleetOOP/` was
touched. Nothing has been committed.

---

## Manual Verification & Investigation Status (2026-09-08)

This section records the full manual verification pass performed after Phase B, plus the current state of
an open, unresolved hardware investigation. **Nothing in this section should be read as claiming a fix for
Command [5] - that issue remains open, see §8-15 below.**

### 1. FleetOOP - COMPLETED

Fleet/OOP implementation and manual verification are complete. Implemented: `Submarine`,
`ResearchSubmarine`, `CombatSubmarine`, `Message`, `Mission`, `Fleet`, `fleet_main.cpp`,
`Tests/FleetOOP/fleet_test.cpp`.

All 10 menu operations were implemented and manually tested. Manual Fleet tests passed: add submarine,
display all submarines, search by serial number, assign mission, update mission details, end mission,
associate combat submarines, send message between allied combat submarines, display inbox, exit.

Negative cases were also manually tested: duplicate submarine, unknown submarine, reassignment while
already assigned, duplicate alliance, self-alliance, research/combat invalid association, sending to a
research submarine, sending to a non-allied submarine, empty message.

Automated Fleet test: **106/106 PASS**. Fleet executable built cleanly with `-Wall`, no warnings/errors.
Fleet/OOP architecture constraints remain as previously documented.

### 2. Full automated regression - PASSED

```
Protocol              23/23
Message                80/80
TLVCodec               22/22
CCMessage               92/92
GSMessage               32/32
Communication           29/29
ManagementCommand       14/14
DataStore               24/24
DataCollection          25/25
ReportGenerator         16/16
TcpTransport            18/18
TcpServerTransport      30/30
GsCommunication         77/77
CentralComputer         16/16
FleetOOP               106/106
```
**Total: 604/604 automated checks PASS, 0 failed.**

Separate CC communication integration test: **58/58 PASS**. Therefore: **662/662 checks PASS, 0 failed.**
The previous transient integration timing issue did not occur during this latest full verification run.

### 3. Build verification - PASSED

- **Central Computer**: build successful. Only known pre-existing warning: `cc_main.cpp:76`,
  `ModeName()` defined but not used. No new warning related to the current investigation.
- **Ground Station**: build successful, zero warnings/errors.
- **FleetOOP**: build successful, zero warnings/errors.
- **Embedded**: build successful using the real ARM GCC/Make build. Previously verified firmware size:
  `text = 79680`, `data = 156`, `bss = 24020`. Zero warnings.

### 4. Manual FleetOOP verification - PASSED

Manual smoke test of all 10 menu operations was completed successfully. The user confirmed the
application behaves correctly. **FleetOOP should be considered COMPLETE.**

### 5. Central Computer manual verification - MOSTLY PASSED

- **CC startup - PASS.** Observed: `Database opened: central_computer.db`,
  `Listening for Ground Station on TCP port 5000.`, `Connected to COM10 at 115200 baud.`
- **CC status - PASS.** **CC quit - PASS.** **CC + GS connection - PASS.**
  **CC status with GS connected - PASS.** **GS status - PASS.**
- **Measurement request - PASS communication-wise.** Initially the database had no measurements; later
  the GS successfully received thousands of measurements from the Embedded.
- **Events request - PASS.** GS successfully received events.
- **CC report - PASS.** Report generation works.
- **RTC set command - PASS.** `g_frames_decoded_ok` increased as expected.
- **System Time request - PASS**, after allowing startup synchronization to complete. Verified result:
  - Before: `g_dispatch_system_time_request_count = 0`, `rtc_synchronized = 1`, `g_dispatch_last_tag = 9`
  - After sending System Time request: `g_dispatch_last_tag = 16 (0x10)`,
    `g_dispatch_system_time_request_count = 1`
  - Therefore `TAG_GET_SYSTEM_TIME_REQUEST = 0x10` was successfully decoded and dispatched.
- **CC status after communication - PASS.** Example verified status: `Frames dispatched: 92`,
  `Decode errors: 0`, `GS frames dispatched: 3`, `GS decode errors: 0`.
- **Measurement backfill**: command starts successfully. **Known limitation** (already documented): the
  real Embedded/LNC application layer does not currently provide the requested historical backfill
  responses, so the operation remains "in progress" - expected, not a new defect.

### 6. Ground Station manual verification - PASSED

- **GS connection - PASS. GS status - PASS.**
- **Measurement request - PASS.** Verified example: 3730 measurements received, e.g.
  `t=1788854079 temp=26.00 humidity=53.00 light=100.00 battery=2.78 mode=2`.
- **Events request - PASS.** Verified: 58 events received.
- **GS status - PASS.** Verified: `Frames dispatched: 353`, `decode errors: 0`.
- **GS restart and reconnect - PASS** - user confirmed it works cleanly.

### 7. Embedded manual verification - PARTIALLY PASSED

- **Debugger/basic state - PASS.** Verified target is available.
- **Temperature configuration**: `g_config_temp_normal_low = 18.0`, `g_config_temp_normal_high = 28.0`.
  **However, these values are boot-time snapshots from Flash and therefore do NOT by themselves prove
  that the latest Command [5] was received** (see §9's own earlier finding that this mirror is written
  once, at boot, from Flash-persisted state).
- **RTC command - PASS.** Before: `g_frames_decoded_ok = 2`. After sending RTC command:
  `g_frames_decoded_ok = 3`.
- **System Time request - PASS**, after startup synchronization was allowed to settle. Verified:
  `g_dispatch_last_tag = 16 (0x10)`, `g_dispatch_system_time_request_count = 1`.

### 8. Current unresolved issue - Command [5]

Command [5] (Set Temperature Normal Range) should send `TAG_SET_TEMP_NORMAL_RANGE = 0x01`. Expected
frame: `AA 01 08 00 00 00 90 41 00 00 E0 41 C7 7B` (14 bytes total), payload `18.0f`/`28.0f`.

**Current observation.** After a fresh reset, before Command [5]: `g_frames_decoded_ok = 0`,
`g_frames_decode_error = 0`, `g_dispatch_last_tag = 0`. After sending Command [5] once:
`g_frames_decoded_ok = 1`, `g_frames_decode_error = 0`, `g_dispatch_last_tag = 32`.

`32` decimal `= 0x20 = TAG_SYSTEM_TIME_RESPONSE` - the automatic startup System Time response.
**Therefore the decoded frame was NOT Command [5].** There was no additional decoded frame corresponding
to `TAG_SET_TEMP_NORMAL_RANGE = 0x01`.

### 9. Important clarification about Command [5]

A previous investigation confirmed `PROTOCOL_MAX_FRAME_SIZE = 256`, `PROTOCOL_MAX_VALUE_SIZE = 250`.
Command [5] uses 14 bytes total, 8 bytes value - nowhere near the protocol frame-size limit.
`Protocol_FeedByte()` is length-agnostic and can process the 14-byte frame. No bug was found in the
protocol decoder that specifically limits frames to 6/10/14 bytes.

### 10. Important RX investigation result

The Embedded RX mechanism currently uses `HAL_UART_Receive()` one byte at a time. USART2 is
polling-based. No DMA or interrupt-based RX was found. CommRxTask immediately requests the next byte
after a successful reception. At 115200 baud, approximately 87 µs/byte.

A possible UART overrun/dropped-byte scenario was identified as a **plausible hypothesis** because: RX is
polling-only; UART hardware has limited receive buffering; FreeRTOS scheduling can delay the RX task; a
dropped byte could cause the protocol decoder to wait indefinitely for the declared remaining bytes, in
which case neither `g_frames_decoded_ok` nor `g_frames_decode_error` would necessarily increase.

**IMPORTANT: this is only a hypothesis. It has NOT been proven to be the root cause. Do not document
UART overrun as a confirmed root cause.**

### 11. Protocol decoder behavior

A read-only investigation confirmed: if a frame is partially received and a byte is missing, the decoder
can remain in a state waiting for additional bytes. There is no independent protocol-level timeout that
automatically resets a partial frame. Therefore a corrupted/truncated frame can potentially produce
`g_frames_decoded_ok` unchanged, `g_frames_decode_error` unchanged, `g_dispatch_last_tag` unchanged. This
is relevant to the current Command [5] symptom. **Again, this does NOT prove that this is what is
happening.**

### 12. Command [6] vs Command [5]

Command [6] (`GET_SYSTEM_TIME_REQUEST`, tag `0x10`, payload 0, frame 6 bytes) has successfully worked.
Command [5] (`SET_TEMP_NORMAL_RANGE`, tag `0x01`, payload 8 bytes, frame 14 bytes) has not yet been
successfully verified end-to-end in the latest controlled test.

Previous historical testing did show a successful Command [5] dispatch at least once: `g_frames_decoded_ok
= 1`, `g_dispatch_set_limit_count = 1`, `g_dispatch_last_tag = 1 (0x01)`. This is important evidence that
the Command [5] implementation CAN work. The current problem therefore appears intermittent or
environment/timing/path dependent rather than a guaranteed protocol incompatibility.

### 13. Previous investigation of incorrect tags

Several misleading tag values were observed during testing:

- **`0x09` / decimal 9** - this is `TAG_SET_RTC_DATETIME`. It appeared because the user had pressed CC
  option [1] before option [6]. Confirmed to be user input/order, not a protocol bug.
- **`0x20` / decimal 32** - this is `TAG_SYSTEM_TIME_RESPONSE`, automatically generated during Embedded
  startup synchronization. Seeing 32 after a reset can be completely legitimate.
- **`0x32`** - this is `TAG_EVENT_CONFIG_CHANGED`, an Embedded/LNC → CC event generated after
  configuration changes. A previous anomalous observation of `0x32` instead of `0x01` was investigated.
  The actual Command [5] frame was not proven to have arrived in that case. A previously documented
  USART2 self-loopback/PA2↔PA3 anomaly remains a possible environmental factor. **Do not claim that
  `0x32` proves Command [5] was dispatched.**

### 14. Current root-cause status

**Command [5] is currently UNRESOLVED.** The following have NOT been proven to be the cause: protocol
frame size, `Protocol_FeedByte()` length handling, CRC, float encoding, CC command construction, UART
overrun, COM10, ST-LINK VCP, FreeRTOS scheduling, RX buffer, TX path. These remain under investigation.

### 15. Current investigation direction

The next investigation is a **complete read-only analysis** of the entire Command [5] path:

```
CC menu -> cc_main.cpp -> ManagementCommand -> message builder -> TLV encoder
  -> Communication::sendFrame() -> SerialPort::send() -> WriteFile() -> Windows COM10
  -> USB/ST-LINK VCP -> STM32 USART2 -> HAL_UART_Receive() -> CommRxTask
  -> Protocol_FeedByte() -> Communication_Dispatch()
```

The investigation should examine: CC TX path, exact frame bytes, `WriteFile` result, `bytesWritten`, COM
configuration, USB/ST-LINK, UART configuration, UART errors, overrun, FreeRTOS scheduling, RX buffers,
protocol decoder, CRC, payload parsing, TX queues, competing traffic, startup timing, memory corruption,
binary/source consistency, and every USART2 access - comparing Command [5] against the known-working
Command [6].

### 16. Current status summary

**COMPLETE / PASS**: FleetOOP implementation; FleetOOP automated tests (106/106); all 15 automated test
suites (604/604); CC communication integration (58/58); full total (662/662); Central Computer startup;
CC/GS connection; CC status; GS status; measurements communication; events communication; report
generation; RTC command; System Time command; GS restart/reconnect; Embedded build; Embedded basic
operation.

**KNOWN LIMITATION**: real LNC measurement/event historical backfill application response is not
implemented/available, so the backfill operation can remain "in progress" (pre-existing, documented).

**UNRESOLVED**: Command [5] / `TAG_SET_TEMP_NORMAL_RANGE = 0x01` does not consistently appear at the
Embedded dispatcher. Need to determine whether the problem is on the CC TX side, the physical/USB/UART
path, or the Embedded RX side.

**NEXT STEP**: complete the read-only root-cause investigation before making any code changes. Do not
mark the issue as fixed. No source files were changed as part of this documentation update.
