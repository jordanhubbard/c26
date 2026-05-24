# MAC Inception Proof

The c26 inception proof is driven from the MAC repository:

```bash
cd ~/Src/mac
.venv/bin/python scripts/prove-c26-inception.py --project-path ~/Src/c26
```

The proof starts with only the c26 name and description, then creates:

1. A top-level epic with design goals and planning instructions.
2. An initial implementation plan derived from that epic.
3. An independent plan review from a different agent.
4. A revised plan that applies review feedback.
5. A fan-out implementation task graph with multiple agents running in parallel.
6. Slack notifier configuration and delivered progress notifications.
7. A final demo story that asks Slack users to run `make smoke` and `make run`
   and provide feedback.

The proof is considered ready only when every MAC task is completed, the Slack
notifier delivered progress, and the demo request includes build and feedback
instructions.
