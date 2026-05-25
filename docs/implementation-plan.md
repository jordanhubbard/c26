# c26 Implementation Plan

This document outlines the implementation plan for the next development phase of the c26 project, a reimagining of the Commodore 64 on RISC-V with a QEMU-first development approach.

---

## Epics and Linked Stories

### 1. Runtime and Kernel Foundation

- **User-visible result:** Stable runtime environment with kernel services supporting device fabric and basic OS functionality.
- **Files likely touched:** `src/runtime.c`, `src/kernel.c`, `include/c26.h`
- **Dependencies:** None
- **Suitable agent type:** Rocky (system-level engine)
- **Verification command:** `make smoke`
- **Expected artifact:** `build/c26.elf` passes smoke test markers

**Stories:**

1.1 **Initialize Kernel Boot and Stack Setup**

- Objective: Implement early boot, stack initialization in assembly, transition to C kernel main.
- Files: `src/boot.S`, `src/kernel.c`
- Dependencies: None
- Agent: Rocky
- Verification: `make smoke`
- Smoke markers: UART boot messages
- Parallelizable: Yes

1.2 **Runtime Services and Device Fabric Initialization**

- Objective: Provide basic runtime services, initialize device fabric messaging.
- Files: `src/runtime.c`, `include/c26.h`
- Dependencies: 1.1
- Agent: Rocky
- Verification: `make smoke`
- Smoke markers: Device fabric init logs
- Parallelizable: No

---

### 2. BASIC Interpreter Demo

- **User-visible result:** Functional BASIC demo showcasing interpreter and interaction.
- **Files likely touched:** `src/basic.c`, `src/runtime.c`, `include/c26.h`
- **Dependencies:** Runtime and Kernel Foundation
- **Suitable agent type:** Natasha (language/runtime specialist)
- **Verification command:** `make smoke`
- **Expected artifact:** BASIC demo output markers in smoke test logs

**Stories:**

2.1 **Implement BASIC Command Parser and Interpreter Core**

- Objective: Add parsing and interpreting for BASIC commands (`PRINT`, `LET`, `PEEK`, `POKE`).
- Files: `src/basic.c`
- Dependencies: 1.2
- Agent: Natasha
- Verification: `make smoke`
- Smoke markers: BASIC READY
- Parallelizable: No

---

### 3. Desktop and Graphics Interface

- **User-visible result:** Desktop banner and graphical display with messages.
- **Files likely touched:** `src/desktop.c`, `src/graphics.c`, `include/c26_devices.h`
- **Dependencies:** Runtime foundation
- **Suitable agent type:** Bullwinkle (UI and UX focus)
- **Verification command:** `make smoke`
- **Expected artifact:** Graphical and desktop markers in smoke logs

**Stories:**

3.1 **Implement Retro Desktop Banner Display**

- Objective: Render desktop banner and user messages.
- Files: `src/desktop.c`, `src/graphics.c`
- Dependencies: 1.2
- Agent: Bullwinkle
- Verification: `make smoke`
- Smoke markers: C26 DESKTOP
- Parallelizable: Yes

---

### 4. Device Fabric and Audio

- **User-visible result:** Audio subsystem and device fabric message handling.
- **Files likely touched:** `src/devices.c`, `src/audio.c`, `include/c26_devices.h`
- **Dependencies:** Runtime and graphic systems
- **Suitable agent type:** Rocky (hardware interface engine)
- **Verification command:** `make smoke`
- **Expected artifact:** Device and audio message markers in smoke output

**Stories:**

4.1 **Implement Device Message Fabric and Audio HAL**

- Objective: Setup audio subsystem and device fabric message passing.
- Files: `src/devices.c`, `src/audio.c`
- Dependencies: 1.2, 3.1
- Agent: Rocky
- Verification: `make smoke`
- Smoke markers: Audio messages in output
- Parallelizable: No

---

### 5. Robot SDK Demo

- **User-visible result:** Robot SDK demos working on top of runtime and graphics.
- **Files likely touched:** `src/robot.c`, `src/runtime.c`, `include/c26.h`
- **Dependencies:** Runtime, graphics, and device systems
- **Suitable agent type:** Natasha (SDK and demo specialist)
- **Verification command:** `make smoke`
- **Expected artifact:** Robot SDK demo markers in smoke logs

**Stories:**

5.1 **Implement Basic Robot SDK Motor and Sensor Examples**

- Objective: Provide example motor and sensor control demos in the SDK.
- Files: `src/robot.c`
- Dependencies: 1.2, 3.1, 4.1
- Agent: Natasha
- Verification: `make smoke`
- Smoke markers: ROBOT SDK DEMO
- Parallelizable: No

---

### 6. Final Demo & User Feedback

- **User-visible result:** Full demo that Slack users can try, including build and run instructions.
- **Files likely touched:** `docs/implementation-plan.md`, `README.md`
- **Dependencies:** All preceding epics
- **Suitable agent type:** Bullwinkle (planner and documenter)
- **Verification command:** Documentation and git checks (`git diff --check`)
- **Expected artifact:** Slack feedback request posted, documented run instructions

**Stories:**

6.1 **Write Final Demo Build and Run Instructions**

- Objective: Document how to build, run, and provide feedback for demo users.
- Files: `docs/implementation-plan.md`, `README.md`
- Dependencies: All epics
- Agent: Bullwinkle
- Verification: `git diff --check`
- Smoke markers: None required
- Parallelizable: Yes

---

## Task Fan-out Order

- Epics 1 (Runtime/Foundation), 3 (Desktop/Graphics), and 4 (Device/Audio) can start in parallel as they operate on mostly independent subsystems.
- Epic 2 (BASIC Demo) depends on completion of Epic 1.
- Epic 5 (Robot SDK Demo) depends on Epics 1, 3, and 4.
- Epic 6 (Final Demo & Feedback) must wait for all others to complete.
