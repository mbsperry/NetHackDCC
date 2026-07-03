# CLAUDE.md

Guidance for Claude Code when working in this repository.

## Model selection

This project's implementation plan (`docs/PROJECT_PLAN.md`) tags each milestone task with a recommended development model — **[Sonnet]** for well-specified work following a known pattern, **[Opus]** for first-of-its-kind engine integration, protocol/lifecycle correctness, security-critical AI translation, persona voice work, or legal/IP judgment calls (see the legend at the top of the "Milestones" section for the full criteria).

Before starting work on a task from that plan, check its tagged model against the model currently running the session. If they don't match, tell the user and suggest switching (e.g. "this task is tagged [Opus] but we're running Sonnet — want to switch before I start?") rather than silently proceeding on the mismatched model. If the user says to proceed anyway, do so without asking again for that task.
