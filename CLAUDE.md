# c26 Agent Instructions

Follow `AGENTS.md`. The canonical issue store is the MAC hub task ledger for
project `c26`, driven by the current CLI in `~/Src/mac`. Do not use Beads or
Dolt and do not create a separate markdown TODO list.

## Build and Test

```bash
make build
make smoke
```

## Architecture

c26 is a freestanding RV64 C/assembly home computer targeting QEMU `virt`.
Host-side build and smoke automation may use Make and Python. Keep emulated
hardware claims honest and demo-safe.

## Completion

Close the corresponding MAC task, commit and push all repository changes, and
verify `main` is up to date with `origin/main` before ending a session.
