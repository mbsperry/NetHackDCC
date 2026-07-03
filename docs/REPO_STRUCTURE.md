# Repository Structure Plan

Decided 2026-07-03 (with user). Supersedes the single-repo layout sketched in
`PROJECT_PLAN.md` Part 2.

## Decision

Two repositories:

1. **`mbsperry/Vibe-crawler`** (new) — the product monorepo and public face of
   the project. Everything we write lives here: the C engine driver, Node/TS
   game server, React client, protocol schemas, persona packs, and project
   docs.
2. **`mbsperry/NetHackDCC`** (this repo) — slims down to a **pure engine
   fork**: upstream NetHack + our minimal, documented patches, nothing else.
   Pinned into Vibe-crawler as a git submodule.

### Why

- This repo is a true GitHub fork of NetHack/NetHack. That's the right shape
  for engine work (merge upstream, send our fixes back) and the wrong shape
  for a product: buried identity ("forked from NetHack/NetHack"), ~30 years of
  history in every clone, C+JS tooling colliding at the root, and a blurred
  NGPL boundary.
- The Node server talks to the driver **only over the NDJSON protocol**, so
  the natural repo seam is protocol-level. But the driver, protocol schemas,
  and Node server co-evolve constantly through Phase 1 — they must live in one
  repo. The driver compiles against engine *headers*, which the pinned
  submodule provides.
- A submodule pin is the mechanical enforcement of the existing plan rule
  "pin the engine commit, upgrade deliberately."
- Keeping the fork thin makes our engine patches trivially upstreamable (both
  current `linux.500` fixes are generic bugs worth a PR to NetHack/NetHack).

## Vibe-crawler layout

```
Vibe-crawler/
├── engine/
│   ├── nethack/          # git submodule -> mbsperry/NetHackDCC (pinned commit)
│   └── driver/           # C driver (moves from NetHackDCC/server/driver)
├── server/               # Node/TS game server (Phase 1)
├── web/                  # React + TS client (Phase 2)
├── packages/
│   └── protocol/         # shared zod schemas (engine + WS protocols)
├── content/
│   └── personas/         # persona prompt packs (original IP)
├── scripts/
│   └── build-engine.sh   # submodule init + hints setup + make GIT=1 WANT_LIBNH=1
├── docs/                 # PROJECT_PLAN.md, ENGINE_AUDIT.md, REPO_STRUCTURE.md
├── CLAUDE.md             # model-selection guidance (moves from the fork)
├── .gitignore
└── README.md
```

Build flow: `scripts/build-engine.sh` initializes the submodule, runs
`sys/unix/setup.sh hints/linux.500` inside it, and builds
`engine/nethack/src/libnh.a`; `engine/driver/Makefile` points its `REPO` at
`engine/nethack`. CI caches `libnh.a` keyed on the submodule SHA — engine
rebuilds only on deliberate pin bumps.

**Path gotcha carried from Phase 0**: the engine bakes `HACKDIR` (and thus
`SYSCF_FILE`) into `libnh.a` at build time from its own checkout path. The
driver's `DCC_SYSCF_FILE` must match; in the new layout the driver Makefile
passes `-DDCC_SYSCF_FILE="$(abspath engine/nethack)/playground/sysconf"` so
the two can never drift.

## Engine fork (this repo) after the split

- **Keeps**: upstream NetHack source; our engine patches; `docs/engine-patches.md`
  (the patch log — it documents *this* repo's divergence); a slim `CLAUDE.md`
  pointing engine-work sessions at the patch rules (no saved-struct changes,
  log every patch) and at Vibe-crawler for everything else.
- **Loses** (moves to Vibe-crawler): `server/driver/`, `docs/PROJECT_PLAN.md`,
  `docs/ENGINE_AUDIT.md`, `docs/REPO_STRUCTURE.md`, the driver-runtime notes
  section of `engine-patches.md` (those describe host obligations, so they
  move into the driver's README).
- **Branching**: `NetHack-5.0` mirrors upstream (fast-forward only). A new
  `dcc-engine` branch (made the GitHub default) = upstream + our patches,
  applied as clean single-purpose commits. The submodule always pins a
  `dcc-engine` commit. Upstream sync = merge `NetHack-5.0` into `dcc-engine`,
  rebuild, run the driver smoke test in Vibe-crawler, then bump the pin.
- **Upstream candidates**: the two `linux.500` `WANT_LIBNH` fixes
  (`recover: lua_support` dependency; `hacklib.o` in the archive).

## Licensing

Unchanged in substance: everything stays open source. The engine fork is NGPL.
In Vibe-crawler, the driver links `libnethack.a` and is therefore NGPL; the
repo carries NetHack's license file alongside its own notices, and we keep the
whole repo NGPL-compatible per the earlier project decision. The split makes
the boundary *legible*, it doesn't change it.

## Workflow notes

- Claude Code sessions for feature work target **Vibe-crawler** (single
  checkout; submodule init is one script). Engine-patch work happens in
  **NetHackDCC** sessions, ideally as focused PRs.
- The model-selection rule in CLAUDE.md (check task's [Sonnet]/[Opus] tag)
  moves to Vibe-crawler with the plan; the fork keeps a pointer.

## Migration checklist (do now, before Phase 0 tasks 3–6)

Phase 0 continues in Vibe-crawler after step 5; only `server/driver` exists
outside docs today, so the move is cheap.

- [ ] **(user)** Create `mbsperry/Vibe-crawler` on GitHub (empty, private or
      public at your discretion) and add it to the working session.
- [ ] **(fork)** Create `dcc-engine` branch from `NetHack-5.0`; apply the two
      `linux.500` fixes as one clean commit; make `dcc-engine` the GitHub
      default branch.
- [ ] **(app)** Scaffold Vibe-crawler: layout above, root README, `.gitignore`
      (node, C artifacts, `engine/nethack` build outputs stay inside the
      submodule and are ignored by its own gitignore), `scripts/build-engine.sh`.
- [ ] **(app)** Add `engine/nethack` submodule pinned to the `dcc-engine`
      commit; move `server/driver/` → `engine/driver/` with Makefile paths and
      `DCC_SYSCF_FILE` updated; move `docs/` (plan, audit, this file) and
      `CLAUDE.md`.
- [ ] **(app)** Verify: `scripts/build-engine.sh && make -C engine/driver &&
      sh engine/driver/smoke.sh` passes from a fresh clone.
- [ ] **(fork)** Remove migrated files from the working branch; fold the
      driver-runtime notes out of `engine-patches.md` into the driver README;
      leave the slim engine-only CLAUDE.md; merge to `dcc-engine`.
- [ ] **(both)** Update `PROJECT_PLAN.md` Part 2 layout section; resume Phase
      0 task 3 in Vibe-crawler.
