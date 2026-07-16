# Agent Instructions

c26 is a freestanding RISC-V project. The target program must remain C and
assembly only; host scripts may be Python or Make for build and smoke testing.

Use non-interactive commands. Validate changes with:

```bash
make smoke
```

The initial platform is QEMU `virt` with UART output. Keep hardware features
demo-safe unless a real emulated device backend is added.

## MAC Task Ledger

Issues live in the MAC hub task ledger under project `c26`. MAC is implemented
in `~/Src/mac`; use its current source CLI when a globally installed `mac`
binary may be stale:

```bash
~/Src/mac/.venv/bin/mac task ready --project c26 --limit 10
~/Src/mac/.venv/bin/mac task show <task_id>
~/Src/mac/.venv/bin/mac task create "title" --project c26 --description-file=-
~/Src/mac/.venv/bin/mac task close <task_id> --reason="..."
```

- Use `mac task` for all project tracking. Do not create Beads/Dolt state or a
  markdown TODO ledger.
- `.tickets/` is ignored compatibility state, not a source of truth; do not
  create or commit it during normal work.
- Use `mac memory remember <key> "<content>" --project=c26` for durable project
  knowledge when explicitly needed.
- Keep the c26 project active and let the fleet claim dispatchable tasks. Use
  `--no-dispatch` only when intentionally staging work.

## Session Completion

Work is not complete until `git push` succeeds.

1. File follow-up issues with `mac task create --project=c26`.
2. Run `make smoke` when code or build behavior changed.
3. Close finished work with `mac task close`.
4. Commit and push:

   ```bash
   git pull --rebase
   git push
   git status  # must show up to date with origin
   ```

5. Clear stashes and inspect managed task refs with
   `~/Src/mac/.venv/bin/mac repo refs status`.
6. Verify all changes are committed and pushed, then hand off context.

Never leave work merely “ready to push”; resolve push failures and retry.
