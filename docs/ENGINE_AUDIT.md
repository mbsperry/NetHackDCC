# Engine Audit — NetHackDCC (NetHack 5.0 dev fork)

Audit of this source tree for suitability as the engine behind a server-hosted, AI-narrated web dungeon crawler (see [`PROJECT_PLAN.md`](./PROJECT_PLAN.md)).

**Verdict: no blockers.** The fork is unusually well-suited: it already ships a library/headless build path, a callback-based windowport designed for embedding, a first-class command-injection API, and fully structured state/status delivery. Line numbers below are as of commit `aaf601f`.

---

## 1. Library / headless build path (already exists)

- `sys/libnh/` builds NetHack as a library: `libnethack.a` (native, `make WANT_LIBNH=1`) and `nethack.js` + `nethack.wasm` (Emscripten, `make CROSS_TO_WASM=1`). Documented in `sys/libnh/README.md` and `Cross-compiling` §B6. Entry point `sys/libnh/libnhmain.c` (`nhmain()`: early_init → choose_windows → initoptions → init_nhwindows → newgame/restore → `moveloop()`).
- Hints plumbing: `sys/unix/setup.sh hints/<file>`; cross fragments in `sys/unix/hints/include/cross-pre1.500`, `cross-pre2.500`, `cross-post.500`. Lua is a git submodule (`make fetch-lua`).
- Caveat: README notes the libnh path is tested on macOS; the Linux build is Phase 0's first task.
- `sys/libnh/libnhmain.c` also exports a JS bridge (`globalThis.nethackGlobal`) enumerating every constant a host needs — `NHW_*`, `BL_*`, all `GLYPH_*_OFF` offsets, `MG_*` flags, colors, `PICK_*`, plus pointers to `extcmdlist`, roles/races/genders/aligns. We use this list as the authoritative inventory for the driver's `init` event (we do not use the WASM path server-side).

## 2. Shim windowport — the integration seam

- `win/shim/winshim.c` (`SHIM_GRAPHICS`): every windowproc marshals to **one callback** `(name, ret_ptr, fmt, args…)` with a printf-style type format. Native mode registers a C function pointer via `shim_graphics_set_callback()`; Emscripten mode bridges via EM_JS + Asyncify.
- This is effectively a serializable RPC of the entire UI: `putstr`, `print_glyph`, menu quintet, all input prompts. Our C driver consumes it directly and re-emits NDJSON.
- Asyncify caveats are documented in the file itself (re-entrancy warnings around lines ~175 and ~313) — one reason we chose the native driver over WASM-in-Node for the server.
- The full windowproc vtable is `struct window_procs` in `include/winprocs.h`; port registry `win_choices[]` in `src/windows.c`. Generic `genl_*` fallbacks exist for optional procs. The WINCHAIN interposer port (`win/chain/`) can tap all UI calls for debugging without touching core.

## 3. Control flow: game-driven and blocking

- Main loop: `moveloop()` / `moveloop_core()` in `src/allmain.c` — monster moves → per-turn housekeeping (hunger, timeouts, regen) → occupation/multi handling → `bot()` status refresh → `rhack()` for the next command.
- All input funnels through **blocking** windowport callbacks: `nhgetch`, `nh_poskey` (key or map click), `yn_function`, `getlin`, `get_ext_cmd`, and menus (`start_menu`/`add_menu`/`end_menu`/`select_menu`, PICK_NONE/ONE/ANY).
- There is no re-entrant "step the game" API → architecture must keep one long-lived engine instance per session; the web layer shuttles input in and UI events out.

## 4. Command queue — first-class injection API

- `src/cmd.c` (~lines 220–440): `cmdq_add_ec` (command function), `cmdq_add_key`, `cmdq_add_dir`, `cmdq_add_int`, `cmdq_add_userinput` into `CQ_CANNED`. Externs in `include/extern.h:438`.
- `rhack()` pops queued entries **before** polling the keyboard, and queued entries answer mid-command prompts (object letters, directions, counts, yn).
- The engine itself composes multi-step actions this way — verified at `src/cmd.c:4688-4714`, e.g. lines 4706–4709 queue `doapply` + inventory letter + direction + a pre-staged `'y'` for the "Lock it?" prompt. This is exactly the pattern the free-text translator compiles to.
- A lower-level `readchar_queue` exists for raw key streams if ever needed.

## 5. Command table

- `extcmdlist[]` at `src/cmd.c:1667` (struct `ext_func_tab` in `include/func_tab.h`): every command with default key, `#`-name, description, function pointer, and flags (`MOVEMENTCMD`, `PREFIXCMD`, `WIZMODECMD`, `AUTOCOMPLETE`, …).
- Single authoritative source for the AI action tool schema; reverse lookup helpers (`cmd_from_func`) exist.

## 6. Interruption semantics (the "walk down the hall" problem) — already solved

- Occupations (`go.occupation`, set via `set_occupation()`) run once per turn until done and are interrupted by `monster_nearby()` (allmain.c occupation block).
- Run/repeat via `gm.multi`; travel = `dotravel()` (`src/cmd.c:5299`) + `findtravelpath()` (`src/hack.c:1266`) — step-wise, interruptible by the same conditions.
- So "a mob shows up mid-hallway" is native engine behavior we *surface* (driver reports the aborted program + triggering messages), not something we build.

## 7. State access

- Globals reachable from any code sharing the address space (our driver): `u` (`struct you`, `include/you.h` — hp, position, attributes, hunger, intrinsics), `gi.invent` (inventory chain), `svl.level` → `levl` map grid (`struct rm`, `include/rm.h`: believed glyph + real terrain), `fmon` (monster chain), `fobj` (floor objects), `gb.blstats` (double-buffered status cache).
- Status is pushed structured: `status_update(fldidx, ptr, chg, percent, color, colormasks)` (`src/botl.c`) over 27 typed `BL_*` fields (`include/botl.h`), conditions as a bitmask. No screen scraping needed.
- Map cells carry semantic identity: `print_glyph(win, x, y, glyph_info*, bkglyph*)`; the glyph int decodes via `GLYPH_*_OFF` bands (`enum glyph_offsets`, `include/display.h:497`; `glyph_to_mon`/`glyph_to_obj`/`glyph_to_cmap`) to exact monster/object/terrain ids; `glyph_info` also carries ttychar, color, UTF-8, `tileidx` (tile atlas index), and `MG_*` flags (pet/corpse/pile/invisible…).
- Messages: `pline` → `putstr(WIN_MESSAGE)` (`src/pline.c`); under `DUMPLOG` a ring buffer of the last 50 plines is kept (`gs.saved_plines`).
- Options settable at runtime: `parseoptions("name:value")` (`src/options.c:489`).

## 8. Lua runtime layer (optional second channel)

- `src/nhlua.c`: `nh.*` library (getmap, pline, menu, gamestate, pushkey, variable, callback…), read-only `u` table, per-turn hook `moveloop_turn` + lifecycle hooks via `dat/nhcore.lua`, themed rooms (`dat/themerms.lua`), sandbox facility (`NHL_SANDBOX`) for untrusted scripts.
- Levels and quests are all Lua (131 files in `dat/`; `des.*` library registered in `src/sp_lev.c`) — future custom-content path.

## 9. Content & telemetry for the AI layer

- Encyclopedia: `dat/data.base` (291 KB flavor text with wildcard keys; read via `checkfile()` in `src/pager.c`) — grounding source for AI item/creature descriptions.
- `DUMPLOG` end-of-game dump (final map, inventory, conducts, last 50 messages) and `xlogfile` (structured key=value per game: points, role, death, conducts, achievements) — feed recaps and leaderboards.
- `livelog` — real-time structured achievement/event records during play.
- Tile toolchain in `win/share/` (text tile sources + converters) for the browser tile atlas.

## 10. Hosting primitives

- Robust SIGHUP handling: the core swaps in `hup_procs` (`src/windows.c`) and saves cleanly with no UI — the same machinery public dgamelaunch servers rely on. Used for session parking.
- Lockfiles prevent double-attach per playground dir; sysconf supports server policy; panic-save/checkpoint enables crash recovery.
- Saves are binary snapshots (`src/save.c`) — no replay log in-tree; save compatibility rule: never patch saved structs or `EDITLEVEL`.

## 11. License

- NGPL (`dat/license`): strong copyleft with source-availability on distribution; no AGPL-style network clause. Compatible with this fully open-source project. The C driver links `libnethack.a` and is published under NGPL; all engine patches are documented in `docs/engine-patches.md`. `dat/data.base` and tile art carry their own contributor copyrights — keep notices intact.

## Key files

| Area | Files |
|---|---|
| Library build | `sys/libnh/README.md`, `sys/libnh/libnhmain.c`, `Cross-compiling`, `sys/unix/hints/include/cross-pre*.500` |
| Shim windowport | `win/shim/winshim.c`, `include/winprocs.h`, `src/windows.c` |
| Loop & commands | `src/allmain.c`, `src/cmd.c`, `include/func_tab.h`, `include/extern.h` |
| State | `include/you.h`, `include/rm.h`, `include/decl.h`, `src/botl.c`, `include/botl.h`, `include/display.h`, `src/pline.c` |
| Lua | `src/nhlua.c`, `dat/nhcore.lua`, `dat/themerms.lua`, `src/sp_lev.c` |
| Content/telemetry | `dat/data.base`, `src/pager.c`, `include/config.h` (DUMPLOG), `src/end.c`, `src/topten.c`, `win/share/` |
| License | `dat/license` |
