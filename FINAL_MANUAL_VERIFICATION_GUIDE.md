# Final Manual Verification Guide

> **Update**: Stages 1 (Git), 2 (Build), 3 (Automated Tests), and the CC↔GS integration test have already been
> performed for you, from a full clean rebuild, with real results — see `FINAL VERIFICATION REPORT` (delivered
> separately) for the exact numbers. You do not need to redo them; they're kept below so you can independently
> re-check any of them yourself if you want to. **Stages 4 through 9 are the genuinely manual parts** — running
> the interactive programs yourself, typing input, and reading output — and that is where your own verification
> adds something a rebuild can't: confirming the programs actually behave correctly when a person drives them.

## How to use this document

This is a guided procedure, not a script for me to run for you. For every step you will see:

- **COMMAND** — exactly what to type (real commands, real file names, real ports — nothing invented).
- **WHAT IT MEANS** — what is actually happening.
- **EXPECTED** — what you should see.
- **IF IT FAILS** — what to send back so it can be diagnosed, without guessing.

Each test is labeled with its kind, so you always know what you're actually proving:

- **[AUTOMATED]** — a `check()`-based test executable, deterministic, no typing required once launched.
- **[MANUAL]** — you type input and read output yourself.
- **[HARDWARE]** — requires the real STM32 board on COM10.
- **[INTEGRATION]** — exercises two or more real components together.
- **[REGRESSION]** — a suite that must keep passing after every change.

Run everything from `C:\GIT\Submarine` unless a step says otherwise. This guide assumes PowerShell or the Git Bash shell already used throughout this project; either works for the `g++`/`gcc` commands.

---

## Stage 1 — Repository and Git [MANUAL]

### 1.1 Confirm the current branch

**COMMAND**
```
git branch --show-current
```
**WHAT IT MEANS**: shows which branch your working tree is on.
**EXPECTED**: `main`.
**IF IT FAILS**: if this prints something other than `main`, or an error, send me the output before continuing — later steps assume `main`.

### 1.2 See everything that changed

**COMMAND**
```
git status --porcelain
```
**WHAT IT MEANS**: `M` = an existing tracked file was modified; `??` = a new, untracked file/directory. This is the authoritative list of everything different from the last commit.
**EXPECTED** — exactly this set (order may differ):
```
 M CentralComputer/include/communication.h
 M CentralComputer/include/message.h
 M CentralComputer/src/cc_main.cpp
 M CentralComputer/src/communication.cpp
 M CentralComputer/src/message.cpp
 M Embeded/Core/Inc/communication.h
 M Embeded/Core/Inc/message.h
 M Embeded/Core/Src/communication.c
 M Embeded/Core/Src/init.c
 M Embeded/Core/Src/keepalive.c
 M Embeded/Core/Src/main.c
 M Embeded/Core/Src/message.c
 M Embeded/Core/Src/monitor.c
 M Embeded/Core/Src/object_detection.c
 M PROJECT_GUIDE.md
?? CentralComputer/include/central_computer.h
?? CentralComputer/src/central_computer.cpp
?? Embeded/Core/Inc/rtc_sync.h
?? FleetOOP/
?? Tests/CentralComputer/
?? Tests/FleetOOP/
```
The 9 `Embeded/*` + `communication.h/.cpp`/`message.h/.cpp` (CC side) + `rtc_sync.h` entries are **Phase 1** (RTC hardening). `cc_main.cpp` + `central_computer.h/.cpp` + `Tests/CentralComputer/` are **Phase A** (the facade). `FleetOOP/` + `Tests/FleetOOP/` are **Phase B**. `PROJECT_GUIDE.md` carries all three phases' documentation.
**IF IT FAILS**: any file *not* in this list means something unexpected changed — send me the exact extra line(s), don't investigate further yourself.

### 1.3 Inspect what actually changed in a file

**COMMAND**
```
git diff --stat
```
**WHAT IT MEANS**: one line per changed tracked file, with insertion/deletion counts — a size check before reading full diffs.
**EXPECTED**: 15 files, matching the `M` lines above. `PROJECT_GUIDE.md` will have by far the largest line count (it accumulates every phase's write-up).
**IF IT FAILS**: a file with a suspiciously large diff you don't recognize — pick that file for step 1.4 next.

**COMMAND** (pick any one file, e.g.)
```
git diff CentralComputer/src/cc_main.cpp
```
**WHAT IT MEANS**: the actual line-by-line change. Lines starting `-` were removed, `+` were added.
**EXPECTED**: for `cc_main.cpp`, you should see the 5 directly-constructed objects (`Communication comm;`, `DataStore store;`, etc.) replaced by one `central_computer::CentralComputer cc;`, and every menu handler call changed from a local variable to `cc.xxx()` — but every printed string and menu action stays the same text.
**IF IT FAILS**: if you see a printed message's actual *text* changed, or new logic that isn't just "call it through `cc.` instead of a local variable," stop and send me the diff — that would be an unapproved behavior change.

### 1.4 Confirm Phase A / Phase 1 files were not touched again during Phase B

**COMMAND** (PowerShell)
```
Get-Item CentralComputer\src\cc_main.cpp, CentralComputer\include\central_computer.h, CentralComputer\src\central_computer.cpp | Select-Object Name, LastWriteTime
```
**WHAT IT MEANS**: file timestamps. Phase A finished before Phase B started, so these three files should carry an *earlier* timestamp than anything under `FleetOOP/`.
**EXPECTED**: `LastWriteTime` for these three is earlier than the `FleetOOP/` files you check next.
**IF IT FAILS**: a `LastWriteTime` on `cc_main.cpp` that's *newer* than your Fleet/OOP work means it was edited again — send me `git diff CentralComputer/src/cc_main.cpp` immediately.

### 1.5 Confirm Fleet/OOP changes are limited to the approved scope

**COMMAND**
```
git status --porcelain | Select-String -NotMatch "FleetOOP|Tests/FleetOOP|Embeded|CentralComputer"
```
(Git Bash equivalent: `git status --porcelain | grep -vE "FleetOOP|Tests/FleetOOP|Embeded|CentralComputer"`)
**WHAT IT MEANS**: filters out every line belonging to Fleet/OOP, the LNC, or the Central Computer, leaving anything else.
**EXPECTED**: only `PROJECT_GUIDE.md` remains.
**IF IT FAILS**: any other line here (Ground Station, a test outside `Tests/FleetOOP`/`Tests/CentralComputer`, anything else) is out of the approved scope — send it to me.

### 1.6 Confirm `PROJECT_GUIDE.md` contains the Phase B summary

**COMMAND**
```
git diff PROJECT_GUIDE.md | Select-String "Phase B"
```
(Git Bash: `git diff PROJECT_GUIDE.md | grep "Phase B"`)
**WHAT IT MEANS**: confirms the new section was actually added, not just planned.
**EXPECTED**: at least one match, e.g. `+## Phase B — Fleet/OOP — IMPLEMENTED and VERIFIED (2026-09-08)`.
**IF IT FAILS**: no match means the documentation step didn't actually save — send me the full `git diff PROJECT_GUIDE.md | wc -l` line count so we can check the file directly.

---

## Stage 2 — Build Everything [MANUAL]

Every build command below is the **exact** one already used and recorded in `PROJECT_GUIDE.md` and the test files' own header comments — nothing here is invented. `-Wall` (used on every `.cpp`/`.c` compile step) tells the compiler to warn about common mistakes (unused variables, suspicious comparisons, etc.) even though the code still compiles — it's a code-quality signal, not a pass/fail gate by itself.

**The one pre-existing, acceptable warning** you will see on `cc_main.cpp` every time:
```
cc_main.cpp:76:13: warning: 'const char* {anonymous}::ModeName(uint8_t)' defined but not used [-Wunused-function]
```
This has existed since before Phase A and is unrelated to it — `ModeName()` is a small helper kept for potential future use. **Any other warning is new and must be reported, not ignored.**

### 2.1 Build the Central Computer (`cc_main.exe`)

**COMMAND** (from `C:\GIT\Submarine`)
```
cd CentralComputer\build
g++ -std=c++17 -Wall -I ..\include -I ..\..\Common\TLVCodec -I ..\..\Common\Protocol -I ..\..\Common\ThirdParty\sqlite3 -c ..\src\cc_main.cpp -o cc_main.o
g++ -std=c++17 -Wall -I ..\include -I ..\..\Common\TLVCodec -I ..\..\Common\Protocol -I ..\..\Common\ThirdParty\sqlite3 -c ..\src\central_computer.cpp -o central_computer.o
```
**WHAT IT MEANS**: each `-c` line compiles one `.cpp` file into an object file (`.o`) — machine code for that file alone, not yet linked into a runnable program. `-I` tells the compiler where to find `#include`d headers.
**EXPECTED**: `cc_main.cpp` produces the one `ModeName` warning above; `central_computer.cpp` produces **zero** warnings.
**IF IT FAILS**: a compile *error* (not warning) here is serious — send me the exact error text, don't attempt a fix.

**COMMAND** (link everything into the final executable — the full, current object list)
```
g++ cc_main.o central_computer.o communication.o data_collection.o data_store.o management_command.o report_generator.o message.o serial_transport.o gs_communication.o tcp_transport.o tlv_codec.o sqlite3.o -lws2_32 -o cc_main.exe
```
**WHAT IT MEANS**: the linker combines all the `.o` files (most already built from earlier work — you're only recompiling the two you touched) plus `-lws2_32` (Windows' networking library, needed for the TCP Ground Station listener) into one `.exe`.
**EXPECTED**: no output at all means success (exit code 0). `cc_main.exe` appears/updates in the folder.
**IF IT FAILS**: `undefined reference` errors mean an object file is missing or stale — tell me exactly which symbol it names.

### 2.2 Build the Ground Station (`gs_main.exe`)

**COMMAND** (from `C:\GIT\Submarine`)
```
cd GroundStation\build
g++ -std=c++17 -Wall -I ..\include -I ..\..\Common\TLVCodec -I ..\..\Common\Protocol -c ..\src\gs_main.cpp -o gs_main.o
g++ -std=c++17 -Wall -I ..\include -I ..\..\Common\TLVCodec -I ..\..\Common\Protocol -c ..\src\cc_communication.cpp -o cc_communication.o
g++ -std=c++17 -Wall -I ..\include -I ..\..\Common\TLVCodec -I ..\..\Common\Protocol -c ..\src\message.cpp -o message.o
g++ -std=c++17 -Wall -I ..\include -c ..\src\tcp_transport.cpp -o tcp_transport.o
g++ -std=c++17 -Wall -I ..\..\Common\TLVCodec -I ..\..\Common\Protocol -c ..\..\Common\TLVCodec\tlv_codec.cpp -o tlv_codec.o
g++ gs_main.o cc_communication.o message.o tcp_transport.o tlv_codec.o -lws2_32 -o gs_main.exe
```
**WHAT IT MEANS**: `gs_main.cpp` is untouched by any of the three phases — this rebuild simply confirms the Ground Station still compiles/links cleanly today. Note `GroundStation/message.h/.cpp` is its **own, separate copy** of the message layer from the Central Computer's — same wire format, independently compiled, matching the frozen protocol's "implemented independently in two languages/binaries" design (Sec 6).
**EXPECTED**: zero warnings on all five compiles; clean link.
**IF IT FAILS**: send me the exact error.

### 2.3 Build the LNC firmware (`Embeded/`) [HARDWARE-relevant, build itself needs no board]

**COMMAND**
```
cd C:\GIT\Submarine\Embeded\Debug
make -f makefile all
```
(If `make`/`arm-none-eabi-gcc` aren't on your `PATH`, they're at `C:\ST\STM32CubeIDE_2.1.1\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.make.win32_2.2.100.202601091506\tools\bin\` and `...externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740\tools\bin\` respectively — add both to `PATH` for this session first, or invoke them by full path.)
**WHAT IT MEANS**: this is the real STM32CubeIDE-generated makefile, cross-compiling C for the ARM Cortex-M4 in the STM32L476RG, then linking into `Embeded.elf`.
**EXPECTED**: build succeeds, zero warnings/errors (this has been true since Phase 1), and reports flash usage (should be roughly 79–80 KB out of the ~1022 KB available — a big jump here would be worth flagging).
**IF IT FAILS**: send me the exact compiler error line.

### 2.4 Build the Fleet/OOP layer (`fleet_main.exe`)

**COMMAND** (from `C:\GIT\Submarine`)
```
cd FleetOOP\build
g++ -std=c++17 -Wall -I ..\include -I ..\..\CentralComputer\include -I ..\..\Common\TLVCodec -I ..\..\Common\Protocol -I ..\..\Common\ThirdParty\sqlite3 -c ..\src\fleet_main.cpp -o fleet_main.o
g++ -std=c++17 -Wall -I ..\include -I ..\..\CentralComputer\include -I ..\..\Common\TLVCodec -I ..\..\Common\Protocol -I ..\..\Common\ThirdParty\sqlite3 -c ..\src\submarine.cpp -o submarine.o
g++ -std=c++17 -Wall -I ..\include -I ..\..\CentralComputer\include -I ..\..\Common\TLVCodec -I ..\..\Common\Protocol -I ..\..\Common\ThirdParty\sqlite3 -c ..\src\research_submarine.cpp -o research_submarine.o
g++ -std=c++17 -Wall -I ..\include -I ..\..\CentralComputer\include -I ..\..\Common\TLVCodec -I ..\..\Common\Protocol -I ..\..\Common\ThirdParty\sqlite3 -c ..\src\combat_submarine.cpp -o combat_submarine.o
g++ -std=c++17 -Wall -I ..\include -I ..\..\CentralComputer\include -I ..\..\Common\TLVCodec -I ..\..\Common\Protocol -I ..\..\Common\ThirdParty\sqlite3 -c ..\src\fleet_message.cpp -o fleet_message.o
g++ -std=c++17 -Wall -I ..\include -I ..\..\CentralComputer\include -I ..\..\Common\TLVCodec -I ..\..\Common\Protocol -I ..\..\Common\ThirdParty\sqlite3 -c ..\src\mission.cpp -o mission.o
g++ -std=c++17 -Wall -I ..\include -I ..\..\CentralComputer\include -I ..\..\Common\TLVCodec -I ..\..\Common\Protocol -I ..\..\Common\ThirdParty\sqlite3 -c ..\src\fleet.cpp -o fleet.o
```
Then reuse the already-built Central Computer objects (from step 2.1's folder — copy or reference `central_computer.o`, `communication.o`, `data_collection.o`, `data_store.o`, `management_command.o`, `gs_communication.o`, `message.o`, `serial_transport.o`, `tcp_transport.o`, `tlv_codec.o`, `sqlite3.o`) and link:
```
g++ fleet_main.o submarine.o research_submarine.o combat_submarine.o fleet_message.o mission.o fleet.o central_computer.o communication.o data_collection.o data_store.o management_command.o gs_communication.o message.o serial_transport.o tcp_transport.o tlv_codec.o sqlite3.o -lws2_32 -o fleet_main.exe
```
**WHAT IT MEANS**: `-I ..\..\CentralComputer\include` is required because `combat_submarine.h` includes `central_computer.h` (the one, deliberate dependency Fleet/OOP has on the Central Computer). `-lws2_32`/sqlite3 are pulled in transitively even though Fleet/OOP never starts them.
**EXPECTED**: zero warnings across all 7 Fleet/OOP compiles.
**IF IT FAILS**: `combat_submarine.cpp` failing with "central_computer.h: No such file" means an include path is missing — check the `-I ..\..\CentralComputer\include` flag first.

### 2.5 Build the two new test suites

**COMMAND** (`Tests/CentralComputer/`)
```
cd C:\GIT\Submarine\Tests\CentralComputer
gcc -std=c11 -c ..\..\Common\ThirdParty\sqlite3\sqlite3.c -o sqlite3.o
g++ -std=c++17 -Wall -I ..\..\CentralComputer\include -I ..\..\Common\TLVCodec -I ..\..\Common\Protocol -I ..\..\Common\ThirdParty\sqlite3 -c central_computer_test.cpp -o central_computer_test.o
```
(then the same per-file compiles as step 2.4 for the CC objects, and link — see the file's own header comment for the full list, it's identical to `fleet_test.cpp`'s recipe minus the Fleet/OOP classes.)

**COMMAND** (`Tests/FleetOOP/`)
```
cd C:\GIT\Submarine\Tests\FleetOOP
g++ -std=c++17 -Wall -I ..\..\FleetOOP\include -I ..\..\CentralComputer\include -I ..\..\Common\TLVCodec -I ..\..\Common\Protocol -I ..\..\Common\ThirdParty\sqlite3 -c fleet_test.cpp -o fleet_test.o
```
Then compile the 6 Fleet/OOP `.cpp` files (same as 2.4) plus the CC objects, and link (see `fleet_test.cpp`'s own header comment for the exact command — it's the same object list as `fleet_main.exe` with `fleet_test.o` replacing `fleet_main.o`).
**EXPECTED**: zero warnings on every file.
**IF IT FAILS**: send me the exact error.

---

## Stage 3 — Automated Tests [AUTOMATED] [REGRESSION]

Run these **in this order**, from `C:\GIT\Submarine`. Each line is `./<path>` then `Enter`.

| # | Command | Subsystem | Expected checks |
|---|---|---|---|
| 1 | `Tests\Protocol\protocol_test.exe` | TLV framing + CRC-16/CCITT-FALSE (incl. the official `"123456789"→0x29B1` vector) | 23/23 |
| 2 | `Tests\Message\message_test.exe` | LNC-side message parsers (C) | 80/80 |
| 3 | `Tests\TLVCodec\tlv_codec_test.exe` | Shared C++ TLV codec (CC/GS side) | 22/22 |
| 4 | `Tests\CCMessage\cc_message_test.exe` | CC-side message builders/parsers | 92/92 |
| 5 | `Tests\GSMessage\gs_message_test.exe` | GS-side message builders/parsers | 32/32 |
| 6 | `Tests\Communication\communication_test.exe` | CC's `Communication` dispatch | 29/29 |
| 7 | `Tests\ManagementCommand\management_command_test.exe` | CC's outgoing commands | 14/14 |
| 8 | `Tests\DataStore\data_store_test.exe` | SQLite schema, insert, range query, retention | 24/24 |
| 9 | `Tests\DataCollection\data_collection_test.exe` | Live collection + backfill correlation | 25/25 |
| 10 | `Tests\ReportGenerator\report_generator_test.exe` | 7-day aggregate reports | 16/16 |
| 11 | `Tests\TcpTransport\tcp_transport_test.exe` | GS's TCP client (real loopback) | 18/18 |
| 12 | `Tests\TcpServerTransport\tcp_server_transport_test.exe` | CC's TCP listener (real loopback) | 30/30 |
| 13 | `Tests\GsCommunication\gs_communication_test.exe` | CC's GS-facing dispatch + chunking | 77/77 |
| 14 | `Tests\CentralComputer\central_computer_test.exe` | The `CentralComputer` facade (Phase A) | 16/16 |
| 15 | `Tests\FleetOOP\fleet_test.exe` | All 6 Fleet/OOP classes + all 10 menu operations' logic | 106/106 |

**WHAT EACH ONE PROVES**: every line prints `PASS`/`FAIL` per check plus a `=== Summary: N checks run, M failed ===` line. A `FAIL` line names exactly which behavior broke — read that line, don't just look at the summary count.

**Running #14 as the last "regular" step, then #15** gets you the two numbers you asked to confirm:
- Rows 1–14 total: **540 + 16 = 556 checks** → **"Full regression: 556/556"**.
- Row 15 alone → **"Fleet/OOP: 106/106"**.

### 3.1 The one test that needs a helper process first [INTEGRATION]

`Tests\CcCommunication\cc_communication_test.exe` (58 checks) is not in the table above because it needs a fixture server running first — it is a real TCP client/server integration test, not a pure unit test.

**COMMAND** (start the fixture, in its own window/background — from `Tests\CcCommunication\`)
```
.\cc_side_test_server.exe 15970 30
```
**WHAT IT MEANS**: this starts a deterministic stand-in for the real `cc_main.exe`, listening on TCP port **15970** (a test-only port, distinct from the real GS port 5000) for 30 seconds, so `cc_communication_test.exe` has something real to connect to.

**COMMAND** (in a second window, after waiting ~2 seconds for the server to be listening)
```
cd Tests\CcCommunication
.\cc_communication_test.exe
```
**EXPECTED**: `58 checks run, 0 failed`.

### 3.2 Recognizing a transient timing failure vs. a real regression

If `cc_communication_test.exe` reports **1–3 failures**, specifically around lines like `FAIL: a fresh measurement request is accepted`, this has already been observed in this project and is a **timing artifact**, not a code defect: if the client connects before the server has fully finished its own startup, or a previous test run left the port briefly in `TIME_WAIT`, the very first request can race. The fix is simply to **wait 2–3 seconds after starting the server before running the client**, and retry once.

**How to tell the difference for real**:
- **Transient**: failures are only in the first few checks of the file, the failure text mentions something being "refused"/"in progress" unexpectedly, and a clean re-run (fresh server, proper wait) passes 58/58.
- **Real regression**: the *same* specific check fails on a clean, patiently-retried run, or a check that has nothing to do with connection timing fails (e.g. a CRC/parsing check, or a chunk-count mismatch). If you see this, stop and send me the exact `FAIL` line and the full summary — do not retry more than twice.

---

## Stage 4 — Fleet/OOP Manual Testing [MANUAL]

**No other process needs to be running first.** `fleet_main.exe` is fully self-contained (in-memory only, no database file, no network, no board).

**1. Which directory**: `C:\GIT\Submarine\FleetOOP\build`
**2. Exact command to get there**:
```powershell
cd C:\GIT\Submarine\FleetOOP\build
```
**3. Exact command to run it**:
```powershell
.\fleet_main.exe
```
**4. What it should display immediately**:
```
=== Submarine Fleet Management ===

--- Submarine Fleet ---
[1]  Add a submarine
[2]  Display all submarines
[3]  Search for a submarine by serial number
[4]  Assign a mission to a submarine
[5]  Update a submarine's mission details
[6]  End a submarine's mission
[7]  Associate two combat submarines with the same mission
[8]  Send a message between allied combat submarines
[9]  Display the messages a submarine received
[10] Exit
>
```
**Keep this one window open for every test below — they build on each other's state** (e.g. Test 4.7 needs the submarine added in Test 4.1 to still exist). Each test tells you exactly what to type, one line at a time, pressing **Enter** after each line.

**How to stop it safely**: type `10` and press Enter (this is Test 4.23 below) — do **not** close the window with the X button or Ctrl+C, since that skips the normal exit message.

### Test 4.1 — Add a combat submarine (operation 1)
**INPUT**: `1`, then `2` (combat), then `SN-001`, `Barracuda`, `Cmdr. Avi`, `45`
**EXPECTED**: `Added SN-001.`
**WHY**: proves `Fleet::addSubmarine()` accepts a well-formed `CombatSubmarine` and `fleet_main.cpp` correctly routes the type-2 prompts (commander, personnel) instead of the research prompts.
**PASS**: the exact line `Added SN-001.` appears.

### Test 4.2 — Add a research submarine (operation 1, other type)
**INPUT**: `1`, then `1` (research), then `SN-002`, `Coral`, `Deep-sea currents`
**EXPECTED**: `Added SN-002.`
**WHY**: proves the type-1 branch prompts for a research topic instead, and `ResearchSubmarine` is constructed correctly.
**PASS**: `Added SN-002.` appears.

### Test 4.3 — Reject a duplicate serial number (operation 1, rejection)
**INPUT**: `1`, then `2`, then `SN-001` (same as 4.1), `Anything`, `Cmdr. X`, `1`
**EXPECTED**: `Could not add it - serial number SN-001 is already in the fleet.`
**WHY**: proves `Fleet::addSubmarine()`'s uniqueness check — the entire point of the approved `bool` return.
**PASS**: the rejection message appears, and a following `2` (display) still shows only the original `SN-001` (name unchanged).

### Test 4.4 — Display all submarines (operation 2)
**INPUT**: `2`
**EXPECTED**: both `SN-001` (Combat, available, Cmdr. Avi, 45) and `SN-002` (Research, available, Deep-sea currents) printed, each with type-specific fields.
**WHY**: proves virtual `print()` dispatches to the correct override for each concrete type through one loop over `Submarine*`.
**PASS**: both submarines shown with the correct type-specific details.

### Test 4.5 — Search for an existing serial number (operation 3)
**INPUT**: `3`, then `SN-002`
**EXPECTED**: `SN-002`'s details only (Research, Coral, Deep-sea currents).
**WHY**: proves `Fleet::findBySerialNumber()` locates the right object.
**PASS**: only `SN-002`'s details are shown, not `SN-001`'s.

### Test 4.6 — Search for an unknown serial number (operation 3, rejection)
**INPUT**: `3`, then `SN-999`
**EXPECTED**: `No submarine with serial number SN-999.`
**WHY**: proves `findBySerialNumber()` returning `nullptr` is handled safely, not a crash.
**PASS**: the message appears; the program keeps running.

### Test 4.7 — Assign a mission (operation 4)
**INPUT**: `4`, then `SN-001`, `Patrol the northern corridor`
**EXPECTED**: `Mission assigned to SN-001.`
**WHY**: proves `assignToMission()` fires and a `Mission` is appended to `Fleet`'s history.
**PASS**: the message appears; a following `3 SN-001` shows `assigned to a mission` and `Mission: Patrol the northern corridor`.

### Test 4.8 — Reject assigning an already-assigned submarine (operation 4, rejection)
**INPUT**: `4`, then `SN-001` (again), `Second mission`
**EXPECTED**: `Could not assign - unknown serial number, or the submarine is already on a mission.`
**WHY**: proves a submarine can't be double-booked, and the mission description from 4.7 is untouched.
**PASS**: the rejection message appears; `3 SN-001` still shows `Patrol the northern corridor`, not `Second mission`.

### Test 4.9 — Assign a second combat submarine (setup for later tests)
**INPUT**: `1`, `2`, `SN-003`, `Marlin`, `Cmdr. Dana`, `38`, then `4`, `SN-003`, `Patrol the northern corridor`, then `4`, `SN-002`, `Reef survey`
**EXPECTED**: `Added SN-003.` then `Mission assigned to SN-003.` then `Mission assigned to SN-002.`
**WHY**: gives you a second combat submarine on the *same* mission text as `SN-001` (needed for the association tests) and puts the research submarine on a *different* mission (needed for the rejection tests).
**PASS**: all three messages appear as expected.

### Test 4.10 — Update mission details, research type (operation 5)
**INPUT**: `5`, then `SN-002`, `Thermal vents`, `Dr. Levi`
**EXPECTED**: `Updated SN-002.`
**WHY**: proves the menu's `dynamic_cast` correctly identifies `SN-002` as a `ResearchSubmarine` and prompts for a topic + researcher, not a commander/personnel count.
**PASS**: `3 SN-002` now shows `Research topic: Thermal vents` and `Researchers: - Dr. Levi`.

### Test 4.11 — Update mission details, combat type (operation 5)
**INPUT**: `5`, then `SN-001`, `Escort the convoy`, `Cmdr. Yael`, `52`
**EXPECTED**: `Updated SN-001.`
**WHY**: proves the same operation's `dynamic_cast` branches correctly for a `CombatSubmarine`, prompting for mission description/commander/personnel instead.
**PASS**: `3 SN-001` now shows `Mission: Escort the convoy`, `Commander: Cmdr. Yael`, `Personnel: 52`.

### Test 4.12 — Associate two combat submarines (operation 7)
**INPUT**: `7`, then `SN-001`, `SN-003`
**EXPECTED**: `SN-001 and SN-003 are now on the same mission.`
**WHY**: proves `Fleet::associateCombatSubmarines()` links both sides.
**PASS**: `3 SN-001` and `3 SN-003` each list the other under "Allied submarines on this mission".

### Test 4.13 — Reject associating a research submarine (operation 7, rejection)
**INPUT**: `7`, then `SN-001`, `SN-002`
**EXPECTED**: the rejection message (`Could not associate them - ...`).
**WHY**: proves `dynamic_cast<CombatSubmarine*>` correctly fails for `SN-002`, so a research submarine can never become an "ally."
**PASS**: rejection message appears; `SN-001`'s ally list is unchanged (still only `SN-003`).

### Test 4.14 — Reject self-association (operation 7, rejection)
**INPUT**: `7`, then `SN-001`, `SN-001`
**EXPECTED**: the rejection message.
**WHY**: proves `firstSerialNumber == secondSerialNumber` is checked before anything else.
**PASS**: rejection message appears.

### Test 4.15 — Reject a duplicate ally (operation 7, rejection)
**INPUT**: `7`, then `SN-001`, `SN-003` (same pair as 4.12)
**EXPECTED**: the rejection message.
**WHY**: proves `isAllyOf()` is checked so the same association isn't recorded twice.
**PASS**: rejection message appears; `3 SN-001`'s ally list still shows `SN-003` exactly once.

### Test 4.16 — Send a message between allies (operation 8)
**INPUT**: `8`, then `SN-001`, `SN-003`, `Position confirmed`
**EXPECTED**: `Message delivered to SN-003.`
**WHY**: proves `sendMessage()` accepts two allied combat submarines.
**PASS**: the message appears.

### Test 4.17 — Display the received message (operation 9)
**INPUT**: `9`, then `SN-003`
**EXPECTED**: `Messages received by SN-003:` followed by `"Position confirmed" - from Marlin (SN-001)`.
**WHY**: proves the message landed in the *receiver's* inbox with the sender correctly identified.
**PASS**: the exact line above appears.

### Test 4.18 — Reject a message to a non-ally (operation 8, rejection)
**INPUT**: `8`, then `SN-001`, `SN-002`, `Should fail`
**EXPECTED**: the rejection message (`... must be combat submarines associated with the same mission ...`).
**WHY**: `SN-002` is a research submarine, so `dynamic_cast` fails on the receiver side.
**PASS**: rejection message appears; `9 SN-002` still reports "Only combat submarines exchange messages."

### Test 4.19 — Reject an empty message (operation 8, rejection)
**INPUT**: `8`, then `SN-001`, `SN-003`, *(press Enter with no text)*
**EXPECTED**: the rejection message.
**WHY**: proves `content.empty()` is checked.
**PASS**: rejection message appears; `9 SN-003` still shows only the one message from Test 4.17.

### Test 4.20 — Reject an unknown serial number (operation 8, rejection)
**INPUT**: `8`, then `SN-999`, `SN-003`, `Hello`
**EXPECTED**: the rejection message.
**WHY**: proves an unknown sender is refused the same way as any other invalid input, without a crash.
**PASS**: rejection message appears.

### Test 4.21 — End a mission (operation 6)
**INPUT**: `6`, then `SN-001`
**EXPECTED**: `SN-001 is available again.`
**WHY**: proves `endMission()` clears the assignment flag **and** the ally list.
**PASS**: `3 SN-001` shows `available` (not "assigned to a mission"), and `Allied submarines on this mission: none`.

### Test 4.22 — Reject ending an unassigned mission (operation 6, rejection)
**INPUT**: `6`, then `SN-001` (again, right after 4.21)
**EXPECTED**: `Could not end it - unknown serial number, or the submarine is not on a mission.`
**WHY**: proves a submarine that's already available can't be "ended" a second time.
**PASS**: rejection message appears.

### Test 4.23 — Exit (operation 10)
**INPUT**: `10`
**EXPECTED**: `Exiting.` and the program closes.
**WHY**: proves the menu loop terminates cleanly, and — since C++ destructors run automatically as the program exits — every `CombatSubmarine`'s owned `CentralComputer` is destroyed cleanly too (nothing to observe directly, but a crash or hang here would indicate a lifetime problem).
**PASS**: the window closes / the process exits with no error.

---

## Stage 5 — Central Computer Manual Testing [MANUAL] [INTEGRATION if a board is attached]

### 5.1 Start it

**1. Which directory**: `C:\GIT\Submarine\CentralComputer\build`
**2. Exact command to get there**:
```powershell
cd C:\GIT\Submarine\CentralComputer\build
```
**3. Exact command to run it**:
```powershell
.\cc_main.exe
```
**10. Does another process need to be running first?** No — `cc_main.exe` is the first thing you start; Ground Station (Stage 6) connects *to* this, not the other way around.
**11. Order note**: if you plan to also test Ground Station afterward (Stage 6), **leave this window open** and start `gs_main.exe` in a second, separate window — don't close this one first.

**EXPECTED first lines**:
```
=== Central Computer ===
Database opened: central_computer.db
```
then either:
```
Listening for Ground Station on TCP port 5000.
```
or a `WARNING: could not listen on TCP port 5000` if something else already holds that port — see 5.6 below if so. Then either:
```
Connected to COM10 at 115200 baud.
```
or `WARNING: could not open COM10 - continuing without a live connection.` if no board is attached — **this warning is expected and correct** if you have no board plugged in right now; it is not a bug.

**WHY THIS VERIFIES STARTUP**: this is `CentralComputer::start()`'s exact 3-step sequence (database → GS listener → serial port) printed by `cc_main.cpp` reading the facade's accessors afterward — confirming the facade's real construction order matches what was implemented.

### 5.2 Verify the menu appears

**EXPECTED**: the 6-option `--- Menu ---` block plus `[q] Quit`, ending in a bare `>` prompt waiting for input.

### 5.3 Verify polling doesn't block or crash

**ACTION**: do nothing for 10–15 seconds after startup.
**EXPECTED**: the program stays completely idle and responsive — no output, no crash, no CPU spike. Then type `3` and press Enter.
**WHY**: this proves `cc.poll()` (which internally polls both the serial port and the TCP listener every loop iteration) is non-blocking, exactly as `Communication::poll()`/`GsCommunication::poll()` are documented to be.

### 5.4 Verify status (`[3]`)

**INPUT**: `3`
**EXPECTED**: a `--- Status ---` block: `Port open`, `Frames dispatched`, `Decode errors`, `Measurement backfill in progress`, `Event backfill in progress`, `GS listening`, `GS client connected`, `GS frames dispatched`, `GS decode errors`.
**WHY THIS VERIFIES COMMUNICATION STATE**: this is the one place you can directly observe whether real bytes have actually been exchanged, without a debugger.
- If no board is attached: `Port open: no`, `Frames dispatched: 0` — expected.
- If a board **is** attached and has been running a moment: `Port open: yes`, `Frames dispatched` > 0, `Decode errors: 0` (a nonzero value here without a passive capture running to explain it should be reported, not dismissed — see `PROJECT_GUIDE.md`'s own decode-error investigation for how this was analyzed previously).

### 5.5 Verify shutdown (this is also "how to stop it safely")

**INPUT**: type `q` and press Enter (this is the *only* safe way to close `cc_main.exe` — never use the X button or Ctrl+C, since that skips the orderly close-down of the port/database).
**EXPECTED**: `Closing...` then the process exits (no hang, no crash).
**WHY**: proves `cc.stop()` closes the serial port, GS listener, and database in order without an exception or hang.
**PASS**: `Closing...` prints and the window's process ends on its own (the console window itself may stay open showing the prompt, depending on how you launched it — that's normal; the *process* has exited).
**FAIL**: the window hangs for more than a few seconds after typing `q`, or shows an error/crash dialog.

### 5.6 If port 5000 is already in use

**COMMAND** (PowerShell)
```
Get-NetTCPConnection -LocalPort 5000 -ErrorAction SilentlyContinue
```
**WHAT IT MEANS**: shows which process (if any) is bound to TCP port 5000.
**EXPECTED**: either nothing (port free) or one entry — check its `OwningProcess` against `Get-Process -Id <that PID>` to see if it's a stale `cc_main.exe` from an earlier run you forgot to close.
**IF IT FAILS/CONFUSING**: send me the output rather than killing a process yourself if you're not sure what it is.

### 5.7 What manual testing cannot show you

**Not observable manually, covered by automated tests instead**:
- That `Communication::feedByte()`'s CRC/length validation actually rejects a malformed frame byte-for-byte — covered by `communication_test.exe` (29/29) and `protocol_test.exe` (23/23).
- That the `CentralComputer` facade's internal wiring genuinely shares one `Communication` instance across `ManagementCommand`/`DataCollection` rather than accidental copies — covered by `central_computer_test.exe`'s `lastSentFrame` checks (16/16).
- That the database's 7-day retention actually prunes old rows — covered by `data_store_test.exe`/`data_collection_test.exe`.

---

## Stage 6 — Ground Station Testing [MANUAL] [INTEGRATION]

**10. Does another process need to be running first?** Yes — start `cc_main.exe` first (Stage 5.1) and wait for its `Listening for Ground Station on TCP port 5000.` line before starting Ground Station. Ground Station always connects *to* the Central Computer; it never listens itself.
**11. Correct order**: (1) start `cc_main.exe`, (2) confirm it printed the "Listening..." line, (3) *then* start `gs_main.exe` in a second, separate window. To stop: close `gs_main.exe` first (Stage 6.7 below), then `cc_main.exe` (Stage 5.5) — closing them in the opposite order is not wrong, just less tidy, since Ground Station will simply show a disconnected state if the CC vanishes first (see Test 6.2, which deliberately does this on purpose).

### 6.1 Start it

**1. Which directory**: `C:\GIT\Submarine\GroundStation\build`
**2. Exact command to get there** (type this in a **new, separate** PowerShell/terminal window — not the same one running `cc_main.exe`):
```powershell
cd C:\GIT\Submarine\GroundStation\build
```
**3. Exact command to run it**:
```powershell
.\gs_main.exe
```
**EXPECTED**:
```
=== Ground Station ===
Connected to CC at 127.0.0.1:5000.
```
(or `WARNING: could not connect to CC at 127.0.0.1:5000` if `cc_main.exe` isn't running or isn't listening — see below.)

### 6.2 Verify connection failure handling

**ACTION**: close `cc_main.exe` first, *then* start `gs_main.exe`.
**EXPECTED**: `WARNING: could not connect to CC at 127.0.0.1:5000 - continuing without a live connection.` followed by `(Use [1] to retry once the CC is running.)` — and the program still runs, doesn't crash.
**WHY**: proves the GS's own documented "no auto-reconnect, but no crash either" design.
**PASS**: the warning appears, the menu still comes up.

### 6.3 Reconnect (menu `[1]`)

**ACTION**: with `gs_main.exe` still running from 6.2 (disconnected), start `cc_main.exe` now, then in `gs_main.exe`:
**INPUT**: `1`
**EXPECTED**: `[cmd] connect(127.0.0.1:5000) -> ok`.
**WHY**: proves manual reconnection works after the CC becomes available later — the one recovery path this design supports.

### 6.4 Send a request and receive a response (measurements)

**INPUT**: `2`, then when prompted `Enter startTime (Unix epoch):` type a value from well in the past (e.g. `0`), and for `endTime` a value far in the future (e.g. `4000000000`).
**EXPECTED**: `[cmd] requestMeasurements(0, 4000000000) -> sent`, then shortly after, asynchronously:
```
[reply] N measurement(s) received:
  t=... temp=... humidity=... light=... battery=... mode=...
```
(`N` may be 0 if the CC's database is currently empty — that's still a valid, complete reply, not a failure. See `PROJECT_GUIDE.md`'s Phase A hardware verification for why a `0`-result reply is not treated as an error.)
**WHY**: this is the real chunked request/response path (`TAG_GET_MEASUREMENTS_BY_RANGE_REQUEST` → possibly multiple `TAG_MEASUREMENT_CHUNK_RESPONSE` frames → merged and printed once).
**PASS**: the `[reply]` line appears (with 0 or more rows) — not silence, not a crash.

### 6.5 Send a request and receive a response (events)

**INPUT**: `3`, then the same kind of wide `[startTime, endTime]` range.
**EXPECTED**: `[cmd] requestEvents(...) -> sent`, then `[reply] N event(s) received:` with `t=... type=... "..."` rows.
**WHY/PASS**: same reasoning as 6.4, for `TAG_GET_EVENTS_BY_RANGE_REQUEST`.

### 6.6 Verify status

**INPUT**: `4` (`[4] Show status` in the menu)
**EXPECTED**: connection state, frames dispatched, decode errors, and whether a measurement/event request is currently in progress.

### 6.7 How to stop it safely

**INPUT**: type `q` and press Enter (never the X button or Ctrl+C).
**EXPECTED**: the program closes the TCP connection and exits cleanly, no error.
**PASS**: process ends with no hang, no crash dialog.
**Recommended order**: stop `gs_main.exe` (this step) before stopping `cc_main.exe` (Stage 5.5) — not required, but tidier, since it closes the client side of the connection first.

---

## Stage 7 — Embedded Testing [HARDWARE] [MANUAL, unless noted]

### What you need
- The real NUCLEO-L476RG board, connected via USB (this also provides the ST-LINK debug/programming interface and the USART2 virtual COM port — confirmed on this machine as **COM10**).
- STM32CubeIDE installed (already present at `C:\ST\STM32CubeIDE_2.1.1\`), or at minimum `STM32_Programmer_CLI.exe` (found under that install at `...externaltools.cubeprogrammer.win32_2.2.400.202601091506\tools\bin\`) if you only want to flash without opening the IDE.

### 7.1 Flash the current firmware [HARDWARE]

**COMMAND**
```
"C:\ST\STM32CubeIDE_2.1.1\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.400.202601091506\tools\bin\STM32_Programmer_CLI.exe" -c port=SWD -d C:\GIT\Submarine\Embeded\Debug\Embeded.elf -v -rst
```
**WHAT IT MEANS**: `-c port=SWD` connects over the ST-LINK's SWD debug interface; `-d ... -v` downloads and verifies the just-built `.elf`; `-rst` resets the board so it boots the new firmware immediately.
**EXPECTED**: `Download verified successfully` and the board's RGB LED comes on (green, if in Normal mode).
**IF IT FAILS**: a "no ST-LINK found" error means the board isn't recognized — check the USB connection and that COM10 appears in Device Manager, then send me the exact error.

### 7.2 Observe startup behavior [HARDWARE]

**ACTION**: watch the board immediately after 7.1's reset.
**EXPECTED**: RGB LED settles to **green** (Normal mode) within a couple of seconds. No buzzer sounds unless you deliberately trigger the IR sensor.
**WHY**: this is Init/Monitor's very first sampling cycle completing and Event's `→Normal` LED behavior firing.

### 7.3 Verify RX (the board receiving from the PC) [INTEGRATION, needs `cc_main.exe`]

**ACTION**: with the board freshly reset and `cc_main.exe` running and connected (Stage 5), press `1` in `cc_main.exe`'s menu (`Send test command: set the LNC's RTC to the current time`).
**EXPECTED**: `cc_main.exe` prints `sendFrame result: ok`. Then check `3` (status) — `Frames dispatched` should be unaffected by this alone (that counts frames the CC *receives*), but if you have a debugger attached, `g_frames_decoded_ok` (a global in `main.c`) should increase by 1 on the board.
**WHY THIS PROVES RX**: this is a real `TAG_SET_RTC_DATETIME` frame sent over the wire and decoded by the LNC's `CommRxTask`.
**If you don't have a debugger session open**: this is only observable via the debugger-visible globals — say so rather than guessing; it's fine to skip this specific confirmation if a debugger isn't set up right now.

### 7.4 Verify TX (the board sending to the PC) [INTEGRATION]

**ACTION**: with the board running and `cc_main.exe` connected, watch `cc_main.exe`'s `3` (status) output over about 10 seconds (long enough for at least one 6-second KeepAlive interval, once the RTC is synchronized — see 7.7).
**EXPECTED**: `Frames dispatched` increases on its own, with `Decode errors` staying at `0`.
**WHY THIS PROVES TX**: this is the LNC's `CommTxTask` actually transmitting KeepAlive/DataReport/Event frames over USART2, arriving at the CC and being decoded successfully.

### 7.5 Verify tasks are running (requires a debugger)

**ACTION**: attach STM32CubeIDE's debugger (Run → Debug, or "Attach to running target" if you don't want to reset the board), open the FreeRTOS task list view (Window → Show View → SEGGER SystemView, or the CubeIDE "Live Expressions"/RTOS-aware thread view).
**EXPECTED**: 7 tasks listed, matching Sec 7's design: `ObjectDetectionTask`, `MonitorTask`, `CommRxTask`, `CommTxTask`, `InitTask`, `KeepAliveTask`, plus the CubeMX-provided `defaultTask`. (`WatchdogTask` does **not** run any real logic — the watchdog was deliberately removed after causing a hardware bug; the task slot exists but its refresh code doesn't, per `PROJECT_GUIDE.md`'s own Open Question #2.)
**IF THIS ISN'T SET UP**: attaching a debugger while a board is mid-test is genuinely optional for a presentation — if you skip it, say explicitly "tasks not individually verified via debugger this session" rather than assuming.

### 7.6 Verify the TX queues (requires a debugger)

**ACTION**: with the debugger attached, add a live/watch expression for `g_dispatch_last_tag` (in `main.c`) and watch it change as different frame types are sent.
**WHAT IT MEANS**: `tx_queue.c` implements exactly 3 static queues — `s_keepAliveQueue` (depth 2), `s_eventQueue` (depth 8), `s_dataQueue` (depth 4) — drained by `CommTxTask` in that strict priority order (Sec 7's `txQueueKeepAlive > txQueueEvent > txQueueData`).
**EXPECTED**: no direct "queue depth" readout exists without instrumenting the code further (not something to add now) — the *effect* of the priority ordering is what you can observe: KeepAlive traffic (every 6s) is never delayed behind Event/Data traffic.

### 7.7 Verify RTC synchronization (Phase 1) [HARDWARE]

**ACTION**: with the board freshly reset (Backup Domain retains state across a normal reset — see `PROJECT_GUIDE.md`'s Phase 1 Test C) and `cc_main.exe` running, wait a few seconds, then check `cc_main.exe`'s status (`3`).
**EXPECTED**: within a couple of seconds of boot, `Frames dispatched` shows at least the automatic `GET_SYSTEM_TIME_REQUEST`/`RESPONSE` exchange having occurred. With a debugger, `rtc_synchronized` (in `main.c`) reads `1`.
**WHY**: this is the exact mechanism Phase 1 implemented and verified — see `PROJECT_GUIDE.md`'s "Phase 1 — Hardware Test 1 Result" section for the full reference values.

### 7.8 Verify error/timeout/busy handling

**Already covered by an automated/software-level test, not something to newly trigger on the board**: the protocol's malformed-frame rejection and resync-on-next-`0xAA` behavior is exercised by `protocol_test.exe` (23/23) against hand-crafted bad frames — deliberately corrupting real UART bytes to test this on hardware is not a supported/safe manual test and isn't necessary, since the mechanism is already proven in software and was observed working correctly during real hardware use (see the decode-error investigation in `PROJECT_GUIDE.md`).

### Summary — what needs the physical board vs. what doesn't
- **Needs the board**: 7.1, 7.2, 7.3, 7.4, 7.7 (all marked [HARDWARE]).
- **Needs the board + a debugger session**: 7.5, 7.6.
- **Already fully covered by automated tests, not a manual hardware step**: 7.8.

---

## Stage 8 — Communication End-to-End Tests [INTEGRATION]

This project has exactly two real, supported end-to-end chains — use these, not an invented third one.

### Chain A — Ground Station → Central Computer → LNC (a command) → LNC responds

```
gs_main.exe          — not directly involved (GS never talks to the LNC)
```
Actually, per this project's real architecture, the GS never issues LNC commands directly — it only ever asks the **CC** for historical data (Sec 4: "That's it — no other GS responsibilities"). The real chains are:

**Chain A — CC → LNC (command) → LNC → CC (reply)**
1. **START**: `cc_main.exe`, connected to the board on COM10 (Stage 5).
2. **SEND**: press `6` in `cc_main.exe` (`Request the LNC's system time`).
3. **OBSERVE**: `cc_main.exe` prints `sendFrame result: ok`, then shortly after, asynchronously, `[reply] system time response: timestamp=...`.
4. **WHICH COMPONENT RECEIVES IT**: the LNC's `CommRxTask` dispatches `TAG_GET_SYSTEM_TIME_REQUEST`, and `communication.c` replies inline with `TAG_SYSTEM_TIME_RESPONSE`.
5. **RESPONSE YOU SHOULD SEE**: the `[reply] ...` line, with a `timestamp` value close to the real current Unix time (once the board's RTC is synchronized — Stage 7.7).

**Chain B — Real STM32 → CC → SQLite → Ground Station (the full data path)**
1. **START**: board running and synchronized (Stage 7.7), `cc_main.exe` connected to it, `gs_main.exe` connected to `cc_main.exe` (Stage 6.1).
2. **WAIT**: let a real KeepAlive/DataReport cycle or two occur (a few seconds) so at least one real measurement lands in the database — confirm via `cc_main.exe`'s `3` status (`Frames dispatched` increasing) or `4` (report).
3. **SEND**: in `gs_main.exe`, press `2` and request a wide `[startTime, endTime]` range covering "now" (e.g. `startTime` a few minutes ago, `endTime` a few minutes from now, both as raw Unix epoch integers — you can get "now" from `cc_main.exe`'s own report timestamps).
4. **WHICH COMPONENT RECEIVES IT**: `cc_main.exe`'s `GsCommunication` parses the request, queries `DataStore::getMeasurementsInRange()` directly (same database the LNC's live traffic just filled), and replies with one or more chunked responses.
5. **RESPONSE YOU SHOULD SEE**: `gs_main.exe` prints `[reply] N measurement(s) received:` with real rows, each `t=` value close to the current real time — proving the complete `STM32 → UART → CC → SQLite → TCP → GS` chain end-to-end.

---

## Stage 9 — Negative Testing [MANUAL]

Only failure modes this project actually supports and documents — nothing invented.

| Failure | How to trigger | Expected behavior | Why it's correct |
|---|---|---|---|
| Wrong/unknown serial number (Fleet) | Stage 4, Tests 4.6/4.8/4.20 | A specific rejection message, no crash | `Fleet` checks every serial via `findBySerialNumber()` before acting |
| Duplicate submarine (Fleet) | Stage 4, Test 4.3 | Rejected, fleet unchanged | `addSubmarine()`'s uniqueness check |
| Invalid submarine type at the menu | In `fleet_main.exe`, choose `1` then type e.g. `9` for the type | `Unknown type - nothing was added.` | `handleAddSubmarine()` validates the type choice before prompting further |
| Empty message (Fleet) | Stage 4, Test 4.19 | Rejected | `content.empty()` check in `Fleet::sendMessage()` |
| Message to a non-ally / non-combat submarine | Stage 4, Tests 4.18/4.20 | Rejected | `isAllyOf()`/`dynamic_cast` checks |
| GS disconnected from CC | Stage 6.2 | Warning printed, program keeps running, manual `[1]` reconnect available | GS's documented "no auto-reconnect, no crash" design |
| CC: COM10 not available | Stage 5.1 with no board attached | `WARNING: could not open COM10 - continuing without a live connection.`, program keeps running | `cc_main.cpp`'s non-fatal open-failure handling (only a failed *database* open is fatal) |
| CC: GS port already in use | Stage 5.6 | `WARNING: could not listen on TCP port 5000`, program keeps running | Same non-fatal pattern |
| Malformed/corrupted TLV frame | Not a manual hardware test — see 7.8 | Frame rejected, decoder resyncs on next `0xAA`, `Decode errors` counter increments | Frozen protocol's required resync behavior (Sec 6), proven in `protocol_test.exe` |
| A stuck/never-answered request (e.g. LNC backfill) | Press `[2]` in `cc_main.exe` with a board that has no application-layer reply for it | `Measurement backfill in progress: yes` stays `yes` indefinitely | Documented, expected: no retry/timeout exists by design; the LNC currently only counts these requests as "deferred" rather than replying |

**Do not attempt** (not supported by this project, would not prove anything real): a "queue full" test (the TX queues are internal to the firmware and not independently triggerable without new instrumentation), a "device unavailable" test beyond COM10/port-5000 already covered above, or any Ethernet-hardware test (this project has none — GS↔CC networking is TCP loopback on one PC by design).

---

## Stage 10 — Final Verification Checklist

```
[ ] 1.1  Current branch is main
[ ] 1.2  git status matches the expected file list exactly
[ ] 1.3  git diff on cc_main.cpp shows only mechanical facade calls, no behavior change
[ ] 1.4  Phase A/Phase 1 file timestamps predate Fleet/OOP files
[ ] 1.5  No changed files outside FleetOOP/Tests/FleetOOP/Embeded/CentralComputer/PROJECT_GUIDE.md
[ ] 1.6  PROJECT_GUIDE.md contains the "Phase B" section

[ ] 2.1  cc_main.exe builds - only the pre-existing ModeName warning
[ ] 2.2  gs_main.exe builds - zero warnings
[ ] 2.3  Embedded firmware builds - zero warnings/errors
[ ] 2.4  fleet_main.exe builds - zero warnings
[ ] 2.5  Both new test suites build - zero warnings

[ ] 3    All 14 original suites individually pass their documented counts (540 total)
[ ] 3    central_computer_test.exe passes 16/16
[ ] 3    fleet_test.exe passes 106/106
[ ] 3    cc_communication_test.exe passes 58/58 (after starting the fixture, retried clean if needed)
[ ] 3    Full regression confirmed: 556/556

[ ] 4    All 10 Fleet menu operations manually exercised (Tests 4.1-4.23)
[ ] 4    All Fleet rejection/edge cases manually confirmed

[ ] 5    cc_main.exe manually verified: startup, menu, idle/no-crash, status, shutdown

[ ] 6    gs_main.exe manually verified: startup, connect failure, reconnect, measurement request, event request

[ ] 7    Embedded: firmware flashed and boots correctly (if board available)
[ ] 7    Embedded: RX and TX each confirmed via a real cc_main.exe exchange (if board available)
[ ] 7    Embedded: tasks/queues noted as debugger-only, done or explicitly skipped

[ ] 8    Chain A (CC->LNC command->reply) manually observed
[ ] 8    Chain B (STM32->CC->SQLite->GS) manually observed

[ ] 9    Negative tests from the table above performed

[ ] 10   No unexpected files changed (re-check git status one final time)
```

When every box is checked (or explicitly marked "skipped - no board available" for the [HARDWARE] items), you will have personally exercised every layer of this project and understood what each result actually proves.
