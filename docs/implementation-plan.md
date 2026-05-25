# c26 Implementation Plan

This document outlines the implementation plan for the next development phase of the c26 project, a reimagining of the Commodore 64 on RISC-V with a QEMU-first development approach.

---

## Epics and Linked Stories

### 1. Runtime and Kernel Foundation
- **User-visible result:** A stable runtime environment with kernel services supporting device fabric and basic OS functionality.
- **Files likely touched:** `src/runtime.c`, `src/kernel.c`, `include/c26.h`
- **Dependencies:** None
- **Suitable agent type:** Rocky (system-level engine)
- **Verification command:** `make smoke`
- **Expected artifact:** build/c26.elf passes smoke test markers

### 2. BASIC Interpreter Demo
- **User-visible result:** A functional BASIC demo showcasing interpreter and interaction.
- **Files likely touched:** `src/basic.c`, `src/runtime.c`, `include/c26.h`
- **Dependencies:** Runtime and Kernel Foundation
- **Suitable agent type:** Natasha (language/runtime specialist)
- **Verification command:** `make smoke`
- **Expected artifact:** BASIC demo output markers in smoke test logs

### 3. Desktop and Graphics Interface
- **User-visible result:** A desktop banner and graphical display with messages.
- **Files likely touched:** `src/desktop.c`, `src/graphics.c`, `include/c26_devices.h`
- **Dependencies:** Runtime foundation
- **Suitable agent type:** Bullwinkle (UI and UX focus)
- **Verification command:** `make smoke`
- **Expected artifact:** Graphical and desktop markers in smoke logs

### 4. Device Fabric and Audio
- **User-visible result:** Audio subsystem and device fabric message handling
- **Files likely touched:** `src/devices.c`, `src/audio.c`, `include/c26_devices.h`
- **Dependencies:** Runtime and graphic systems
- **Suitable agent type:** Rocky (hardware interface engine)
- **Verification command:** `make smoke`
- **Expected artifact:** Device and audio message markers in smoke output

### 5. Robot SDK Demo
- **User-visible result:** Robot SDK demos working on top of the runtime and graphics
- **Files likely touched:** `src/robot.c`, `src/runtime.c`, `include/c26.h`
- **Dependencies:** Runtime, graphics, and device systems
- **Suitable agent type:** Natasha (SDK and demo specialist)
- **Verification command:** `make smoke`
- **Expected artifact:** Robot SDK demo markers in smoke logs

### 6. Final Demo & User Feedback
- **User-visible result:** A full demo that Slack users can try, including build and run instructions
- **Files likely touched:** `docs/implementation-plan.md`, `README.md`
- **Dependencies:** All preceding epics
- **Suitable agent type:** Bullwinkle (planner and documenter)
- **Verification command:** Safe docs and git checks only (e.g. `git diff --check`)
- **Expected artifact:** Slack feedback request posted, documented run instructions

---

## Parallel Execution Considerations

- Epics 1 (Runtime/Foundation), 3 (Desktop/Graphics), and 4 (Device/Audio) can start in parallel as they cover mostly independent subsystems.
- Epics 2 (BASIC Demo) depends on Epic 1.
- Epic 5 (Robot SDK Demo) depends on Epics 1, 3, and 4.
- Epic 6 (Final Demo & Feedback) must wait for all others.

---

## Final Demo Slack Instructions

- Ask Slack users to build, run, and provide feedback on the latest demo.
- Commands:
  - `make build`
  - `make smoke`
  - Run using QEMU if available (not required for Bullwinkle)
- Request feedback in Slack channel #c26-feedback or DM @Bullwinkle

---

## Notes
- This plan document is maintained in the repository and updated incrementally as development progresses.
- Agents Rocky, Natasha, and Bullwinkle have roles assigned according to their specialization and system capabilities.
- Bullwinkle is responsible for documentation and planning tasks and does not require QEMU installed for this planning stage.
