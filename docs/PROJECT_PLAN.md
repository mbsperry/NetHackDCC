# NetHackDCC Project Plan — AI-Narrated Web Dungeon Crawler

## Vision

A web-based narrative dungeon crawler built on the NetHack engine (this fork, dev branch v5.0). NetHack supplies the hard parts — ~396 monsters, ~450+ objects, ~40 artifacts, combat, inventory, leveling, dungeon generation, decades of business logic. We add:

- A web frontend with a traditional **map view** and an **AI narrative view** — side-by-side on desktop, tabs on mobile.
- GenAI narration in a sarcastic, unstable dungeon-AI announcer voice (Dungeon Crawler Carl-inspired *tone*, **original IP** — our own persona, names, and catchphrases; sarcastic achievements).
- **Free-text action input** translated by AI into game commands; standard NetHack keyboard commands always work too, including map navigation.
- Narration toggleable off; narration must **never block gameplay**.
- AI-enhanced item/creature descriptions grounded in the game's encyclopedia; inventory/stats/message displays.

**Core decisions:**
- Engine runs **server-side**: one headless NetHack instance per player session; browser is a thin client over WebSocket.
- Stack: **React + TypeScript** frontend; **Node.js (TypeScript)** game server.
- **Provider-agnostic AI layer** (Claude / OpenRouter / OpenAI / Gemini behind one interface); fast/cheap tier for routine work, strong tier for set pieces.
- Fully open source under the NetHack General Public License (NGPL).

See [`ENGINE_AUDIT.md`](./ENGINE_AUDIT.md) for the source audit that grounds this plan. Verdict: **no blockers** — the fork already ships a library build (`sys/libnh/`), a callback-based headless windowport (`win/shim/`), and a first-class command-injection API (the command queue in `src/cmd.c`).

---

## Part 1 — Architecture

### Engine bridge: native C driver, one child process per session

`server/driver/` is **new code, not an engine patch**: a small C program that includes `hack.h`, links `libnethack.a`, registers the shim callback, and speaks NDJSON over stdio to the Node server.

Chosen over running the existing `nethack.wasm` + Asyncify artifact inside Node workers because:

- `win/shim/winshim.c` itself documents Asyncify re-entrancy breakage (warnings around lines ~175 and ~313) — too fragile to bet the server on. In a native process, blocking input is just a blocking `read()`.
- Real filesystem: saves, bones, xlogfile, livelog, lockfiles work unchanged in a per-session playground dir (the WASM build embeds data in MEMFS).
- Direct C access to all globals and `cmdq_add_*` — state serialization needs zero engine patches (WASM would need `EXPORTED_FUNCTIONS` edits plus byte-offset struct reads from JS).
- OS-process crash isolation plus NetHack's own panic-save/checkpoint recovery; gdb/ASan debugging; no emsdk toolchain burden.
- ~15–30 MB RSS per session; scale is RAM-bound per node, sticky-route sessions horizontally later.

The WASM build stays untouched as an upstream feature (possible future offline/demo mode).

### Repository layout

```
├── src/, include/, win/, dat/, sys/   # engine (minimal, documented patches)
├── server/driver/       # C: main.c (boot+dispatch), proto.c (NDJSON), snapshot.c, inject.c, Makefile
├── server/node/src/     # TS: session/ (SessionManager, DriverProcess), ws/, ai/, content/, achievements/
├── web/src/             # Vite+React+TS client
├── packages/protocol/   # shared zod schemas for both protocols
├── content/personas/    # persona prompt packs (original IP), achievement templates
└── docs/                # PROJECT_PLAN.md, ENGINE_AUDIT.md, engine-patches.md
```

### Session lifecycle

- **Create**: allocate `var/sessions/<id>/` playground (save/, bones, xlogfile, `.nethackrc`), spawn `nhdcc-driver -d <dir> -u <name>`.
- **Attach**: WebSocket + session token; server replays current snapshot + buffered messages (seamless reconnect).
- **Detach**: grace timer (~10 min) → park via SIGHUP (engine hup-saves cleanly via its `hup_procs` machinery).
- **Resume**: respawn driver; engine restores its save natively.
- **Crash**: nonzero exit → respawn once, panic-save restore; double-crash → surface error, preserve dir for debugging.
- Engine lockfiles prevent double-attach; the server enforces one WS writer per session. SQLite session store.

### Protocols (shared types in `packages/protocol/`)

**Engine protocol (driver ↔ Node, NDJSON over stdio).** Invariant: at most one pending `ask` at a time (the engine is blocking and game-driven).

- Driver → Node: `init` (constants, glyph offsets, extcmds), `map` (batched cells with symbolic ids), `msg`, `status` (typed fields), `inv`, `menu_ask`, `ask` (yn/line/extcmd/poskey/getch/dir), `turn` (boundary marker), `snapshot`, `program` (done/aborted + reason), `livelog`, `exit` (+xlog).
- Node → Driver: `answer` (correlated by ask id), `inject` (command program: `{op:"ec"|"key"|"dir"|"int"|"expect"}` steps), `snapshot`, `interrupt` (cmdq_clear + ESC), `set_option`, `save`.
- `expect` steps are driver-side guards: if the next ask doesn't match the predicted prompt, flush the command queue, emit `program aborted`, and forward the ask to the player. **This is the safety valve** for AI-composed command programs.
- Snapshots are serviced inside any shim callback (shared address space; display callbacks fire even during long occupations).

**WebSocket protocol (Node ↔ browser).**

- Server → client: `full_map`/`map_diff`, `status`, `msg`, `inv`, `menu_open`/`menu_close`, `prompt`, `narration` deltas / `narration_end`, `achievement`, `describe_result`, `action_status` (free-text plan progress), `session`.
- Client → server: `key` (raw keymap passthrough), `map_click`, `menu_select`/`menu_cancel`, `prompt_answer`, `freetext`, `describe`, `set` (narration density, ascii/tiles, persona).

### Engine patches (kept minimal, each logged in `docs/engine-patches.md`)

1. Linux hints/target for `WANT_LIBNH` (README says macOS-tested only) — likely build fixes.
2. De-static any needed symbol (probably none — `extcmdlist`, `cmdq_*` are already extern).
3. Turn-boundary callout: 2–3 lines in `moveloop_core()` emitting a `shim_turn_end` callback under `#ifdef SHIM_GRAPHICS` (for narration batching).
4. Livelog: tail the file first (zero patch); add a shim callback only if that proves racy.
5. **Rule: never touch saved structs or `EDITLEVEL`** — saves stay compatible with upstream; pin the engine commit, upgrade deliberately.

---

## Part 2 — Frontend plan (`web/`)

Vite + React + TypeScript; zustand store fed by a WS reducer mirroring session state (map grid, status, message ring, pending ask, menus, narration streams).

- **MapCanvas**: `<canvas>`, two modes — ASCII (UTF-8 char + color per cell) and tiles (PNG atlas built from `win/share/*.txt` via the existing tile toolchain, indexed by `tileidx`). Cell-diff rendering, cursor, click→travel/target, hover tooltip with symbolic id + "describe" affordance.
- **StatusBar**: the 27 `BL_*` fields with colors + condition badges.
- **MessageLog**: scrollback (no tty `--More--`; the shim delivers messages individually).
- **InventoryPanel**: perm_invent (enabled via `set_option`), grouped by object class.
- **MenuOverlay / PromptBar**: the pending ask drives one input-mode state machine — PICK_NONE = dismissable overlay, PICK_ONE = click list, PICK_ANY = checkboxes + confirm, yn = inline bar with buttons *and* raw key, poskey = map target mode. Raw keys always answer the open ask (tty-equivalent behavior).
- **NarrativePane**: streaming text, turn anchors, achievement toasts; unmounts entirely when narration is off.
- **FreeTextInput**: under the narrative pane; progress chips from `action_status`; keyboard passthrough disabled while focused.
- **Layout**: desktop CSS grid (map+status | narrative, toggleable); mobile tabs [Map | Story | Inventory | Log] + on-screen direction pad; settings sheet (density, persona, ascii/tiles).
- Full NetHack keymap passthrough including Ctrl combos and count prefixes.

**Gate: "classic web NetHack" is fully playable before any AI code lands.**

## Part 3 — AI integration plan (`server/node/src/ai/`)

- **Provider layer**: `LLM` interface (stream/complete) with Anthropic / OpenAI / OpenRouter / Gemini adapters; `fast`/`strong` tier map per task type; retries, timeouts, circuit breaker; token/$/latency metrics from day one.
- **Narration pipeline**: per-session EventBuffer accumulates between `turn` markers (messages, status deltas, livelog) → rule-based significance filter (combat, first sighting of a species, level entry, HP < 30%, death = max; movement spam = 0) → density thresholds (`off|low|med|high`; off short-circuits all LLM calls) → async narration worker (1 in-flight + 1 pending; newer batches coalesce so the story summarizes rather than backlogs; the gameplay input path never touches this code) → streamed to the NarrativePane.
- **Persona system**: `content/personas/<id>/persona.yaml` — system prompt (voice, running gags), style constraints, achievement templates, set-piece triggers routed to the strong tier. Ship one original persona (working concept: a malfunctioning dungeon-management AI). Memory = rolling run summary (fast model, every ~50 turns) + recent narrations.
- **Descriptions**: parse `dat/data.base` at startup (same format `checkfile()` reads) + hard stats from a build-time `monsters.json`/`objects.json` dump tool (links `libnethack.a`) → fast model in persona voice → SQLite cache keyed `(entity, persona, promptVersion)` — near-100% hit rate after warmup; pre-generate the common ~200 entries.
- **Achievements**: triggered only off real engine signals (livelog/xlogfile records + rules on real events); titles generated once per (trigger, persona) and cached; toasts + panel; end-of-game recap from dumplog + xlogfile via the strong tier.
- **Cost controls**: per-session token meter; soft budget → auto-drop density (in-voice "budget cuts"); hard budget → narration off, gameplay unaffected; `$ / session` cap in config.

## Part 4 — Free-text action translation

Pipeline: `freetext` → context assembly → LLM intent parse → plan validation → cmdq program → guarded execution.

- **Tool schema** generated from `init.extcmds` (exclude WIZMODECMD/internal; annotate movement/prefix commands) + synthetic tools: `travel_to`, `answer_pending`, `clarify`, `cannot`.
- **Context**: compact snapshot — position, dlvl, HP, hunger, inventory letters+names, visible monsters (hostile/peaceful), objects/features, pending ask, last ~10 messages. Static schema is prompt-cached; latency target < 1.5 s on the fast tier.
- **Compile**: the model emits a JSON plan → each step compiles to `inject` steps with pre-staged answers **only from an allowlist** (item letters, directions, benign yn); every step gets an `expect` guard.
- **Dangerous prompts are never auto-answered**: danger-by-default classifier ("Really attack", "peaceful", "shopkeeper", "pray", save/quit, cursed confirmations…); a dangerous or unexpected ask aborts the program and surfaces the real prompt to the human in both views.
- **Interruption** (the "walk down the hall" problem): the engine already stops travel/occupations on `monster_nearby()` and similar; the driver reports `program aborted {reason:"interrupted"}` with the triggering messages. v1 policy = report and hand control back (no auto-replan; replan-once later behind a setting).
- **Ambiguity**: `clarify` → quick-reply prompt; when confidence is high, act and say what was chosen. **Parse failure**: `cannot` → in-voice error + phrasing hint; raw input is never guessed into a command.
- **Failure modes covered**: hallucinated inventory letters (pre-flight validation + expect abort), stale state (guard abort), unpredicted menus (surface to player), blocked travel (engine stops, reported), prompt injection via user text *or in-game text* — engravings can literally say "ignore previous instructions" (mitigations: schema-constrained output, allowlist-only auto-answers, server-side classifier), LLM outage (free-text disabled, keys always work), count runaway ("search 999 times" → cap injected counts).

## Part 5 — Risks & open issues

1. Linux `WANT_LIBNH` build bit-rot (macOS-tested only) — Phase 0 de-risks this first.
2. Shim coverage gaps (some procs are `genl_*` stubs) — Phase 0 verifies getlin/extcmd/poskey round-trips.
3. Upstream is a dev branch; saves may break between engine updates — pin the commit; no save-struct patches ever.
4. LLM latency vs roguelike pacing — narration is async by design; the UI must visually decouple the panes so narration lag never reads as game lag.
5. Cost per hour — measure in Phase 3; budget controls are a launch requirement, not a nice-to-have.
6. Prompt injection via free text and via in-game text — all text is data; command execution only through the schema + allowlist path.
7. NGPL — the driver links `libnethack.a` and is published under NGPL; notices everywhere, patches documented; the whole project is open source anyway.
8. Original-IP discipline — content review step on persona packs (no DCC names, characters, or catchphrases).
9. Bones sharing under concurrency — start per-session (off), enable shared bones later after lock testing.
10. Scaling — process-per-player caps a node at ~RAM/30 MB sessions; sticky routing across nodes later.
11. Long occupations (resting/digging) — verify snapshot freshness + `interrupt` behavior mid-occupation.

---

## Part 6 — Milestones

### Phase 0 — Engine bridge spike (de-risk everything first)
- [ ] Build `libnethack.a` on Linux (`setup.sh` + `make WANT_LIBNH=1 fetch-lua all`); fix hint issues (patch #1)
- [ ] `server/driver/main.c` v0: register shim callback, boot `nhmain()` in a per-session dir, dump every shim event as NDJSON
- [ ] Canned answers through character creation → first map
- [ ] Decode a `print_glyph` batch to `{x,y,ch,color,monIdx}` via glyph bands
- [ ] Inject the inventory command via `cmdq_add_ec` resolved from `extcmdlist`; observe the menu event
- [ ] Emit a JSON snapshot of `u` + `gi.invent` from inside a callback

**Definition of done**: a headless script walks the `@` one step east and prints correct hp/pos over JSON pipes.

### Phase 1 — Driver + session server + protocol
- [ ] Full driver: all procs → events/asks, answer correlation, snapshots (symbolic ids), `inject` + `expect` + abort, `interrupt`, `set_option`, turn callout (patch #3), livelog tail
- [ ] `packages/protocol/` zod schemas (both protocols)
- [ ] Node server: DriverProcess (spawn/stdio/restart), SessionManager (create/attach/park-SIGHUP/resume/crash-respawn), SQLite store, WS gateway + auth
- [ ] CLI devtool: play over the protocol from a terminal (integration rig)
- [ ] Automated bot test: 200 turns including a menu, a yn, a getlin, save, resume

**Definition of done**: two concurrent sessions play independently; `kill -9` a driver mid-game → the session resumes from panic save.

### Phase 2 — Classic web NetHack (zero AI)
- [ ] Web scaffold, WS client + store, session create/join
- [ ] MapCanvas ASCII mode (colors, cursor, click-to-travel)
- [ ] StatusBar, MessageLog, InventoryPanel (perm_invent)
- [ ] MenuOverlay + PromptBar for all ask kinds; full keyboard passthrough
- [ ] Tile mode (atlas from `win/share`), ASCII/tiles toggle
- [ ] Mobile tabs + on-screen direction pad
- [ ] Save on disconnect, resume on reconnect

**Definition of done**: a NetHack player comfortably plays to Mines' End in a browser, desktop and phone.

### Phase 3 — AI foundation + narration
- [ ] Provider abstraction + 4 adapters, tier config, metrics
- [ ] EventBuffer + significance rules + density settings (unit-tested on recorded Phase-2 event streams)
- [ ] Narration worker (coalescing, streaming) + NarrativePane + toggle (off = zero LLM calls)
- [ ] Persona pack v1 (original snarky dungeon-AI) + picker; run-summary memory
- [ ] Final desktop side-by-side + mobile Story tab

**Definition of done**: a 30-minute session narrated in-voice; narration never delays a keypress (measured); cost logged per session.

### Phase 4 — Descriptions + achievements
- [ ] `data.base` parser/index + build-time stats dump (`monsters.json`/`objects.json`)
- [ ] Describe service + SQLite cache + pre-generation; describe UI on map hover + inventory
- [ ] Achievement engine (livelog/xlogfile + rule triggers), cached titles, toasts + panel
- [ ] End-of-game recap (dumplog + xlogfile, strong tier)

**Definition of done**: describing a kitten twice hits cache; dying yields an in-voice obituary + at least one earned mock-achievement.

### Phase 5 — Free-text actions
- [ ] Tool schema generator + context assembler
- [ ] Intent parse (fast tier, JSON-constrained) + plan validator + compiler to `inject` programs
- [ ] Danger classifier, clarify flow, `cannot` fallback, count caps
- [ ] Interruption UX (abort chips, hand-back); `action_status` in both panes
- [ ] Red-team suite: injection strings, hallucinated items, mid-program menus, shopkeeper scenarios

**Definition of done**: "put on the ring and head down the stairs" works; "attack the shopkeeper" surfaces the real yn prompt to the human — never auto-answered.

### Phase 6 — Hardening, cost, ops, release
- [ ] Budget enforcement + user-visible meter; density auto-degrade
- [ ] Soak: 20 concurrent bot sessions × 24 h; crash drills; memory/fd audits
- [ ] Deployment (containerized server + driver, static web), TLS/WS, session auth
- [ ] NGPL compliance pass (licenses, `docs/engine-patches.md`, source publication); persona IP review; docs

**Definition of done**: public playable instance; a fresh clone builds engine + driver + server + web with documented steps.
