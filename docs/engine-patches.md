# Engine Patches Log

Every change to the upstream engine (`src/`, `include/`, `win/`, `sys/`, `dat/`) made in service of this project, in commit order. Rule: never touch saved structs or `EDITLEVEL` — saves must stay compatible with upstream.

## 1. `sys/unix/hints/linux.500` — fix `recover` target missing `lua_support` dependency under `WANT_LIBNH`

**Problem**: building `libnh.a` on Linux (`make GIT=1 WANT_LIBNH=1 all`) failed with `fatal error: nhlua.h: No such file or directory` while compiling `util/recover.c`.

**Cause**: the `WANT_LIBNH` block overrides `GAME=` (empty), since no game executable is built in library mode. The top-level `recover: $(GAME)` dependency chain, which normally triggers the `lua_support` target (generator of `include/nhlua.h`), becomes a no-op with `$(GAME)` empty. `recover.c` still transitively includes `nhlua.h` via `hack.h`, so the build fails.

**Fix**: `sys/unix/hints/macOS.500` already carries the fix for this — an explicit `recover: lua_support` rule inside the `WANT_LIBNH` block, guarded by `ifdef MAKEFILE_TOP` so it's only added once (at the top-Makefile level, where both `recover` and `lua_support` targets live). `linux.500` was missing this rule; ported it verbatim.

**Verification**: clean rebuild (`rm -f src/libnh.a src/*.o include/nhlua.h && make GIT=1 WANT_LIBNH=1 all`) completes with exit 0, no errors, and `nm src/libnh.a` shows `nhmain`, `shim_graphics_set_callback`, and all four `cmdq_add_*` symbols exported.

## 2. `sys/unix/hints/linux.500` — include `hacklib.o` in `libnh.a`

**Problem**: linking anything against `src/libnh.a` failed with undefined
references to core utility functions — `dist2`, `distmin`, `eos`, `sgn`,
`strncmpi`, `nh_snprintf`, `upstart`, and others.

**Cause**: these live in `src/hacklib.o`, which the normal game/recover builds
pull in via `$(TARGET_HACKLIB)`. The Linux `WANT_LIBNH` archive rule assembled
`libnh.a` from `$(HOBJ) $(LIBNHSYSOBJ)` and the Lua archive only — omitting
hacklib entirely. (`macOS.500` merges it via `$(TARGET_HACKLIB)` in its
`libtool` invocation; `linux.500` had no equivalent.)

**Fix**: add `$(TARGETPFX)hacklib.o` to both the prerequisites and the `ar`
command of the `libnh.a` rule. The plain object merges cleanly with `ar`
(unlike a nested `.a`, whose members a linker won't dereference — which is also
why liblua is still linked separately by consumers rather than relied on inside
`libnh.a`).

**Verification**: `nm src/libnh.a` now lists `dist2`/`eos`/`sgn` as defined
(`T`) symbols, and `server/driver/nhdcc-driver` links and boots the engine.

## Build notes (not engine patches, but required and undocumented)

- **`submodules/lua` must be initialized** before building: `git submodule update --init submodules/lua`.
- **`GIT=1` must be passed on the `make` command line** (in addition to `WANT_LIBNH=1`) to opt into the submodule-based Lua build. Without it, `GITSUBMODULES` is never set (it's gated on `ifeq "$(GIT)" "1"` / `ifeq "$(git)" "1"` in the generated Makefile, not on submodule presence), and the build falls back to the tarball-fetch Lua path (`lib/lua-5.4.8/...`), which fails with "Please do 'make fetch-lua'" since no tarball was fetched. `sys/libnh/README.md`'s quick-start doesn't mention this flag — worth a doc fix upstream.
- Full working build sequence on Linux:
  ```
  git submodule update --init submodules/lua
  cd sys/unix && sh setup.sh hints/linux.500 && cd ../..
  make GIT=1 WANT_LIBNH=1 all
  ```
  Output: `src/libnh.a`.

## Runtime notes (behaviour a host must accommodate; no engine change)

- **`CHDIR` is not defined in this `libnh` build.** Consequences the driver
  handles: the `-d <playground>` command-line option is inert (its value also
  breaks the `-u`/option scan if passed), and `nhmain` never `chdir()`s to the
  playground itself. The host must `chdir()` into the per-session dir before
  calling `nhmain` so relative paths (`nhdat`, `symbols`, `perm`, `record`,
  `save/`) resolve there. Setting `$NETHACKDIR` is kept as belt-and-suspenders
  for a future `CHDIR` build.
- **`SYSCF_FILE` is a compiled absolute path** (`HACKDIR/sysconf`,
  i.e. `<repo>/playground/sysconf`), read regardless of the working dir. The
  host must ensure that file exists; `MAXPLAYERS` there is capped at 25.
- **Runtime files the engine expects to pre-exist** in the playground:
  empty `perm`, `record`, `logfile`, and a `save/` directory (a normal
  `make install` creates these). Data files `nhdat`, `symbols`, `license` are
  symlinked in from `dat/`.
- **Player selection**: with the shim's no-op `player_selection`, `newgame()`
  → `role_init()` fills any unspecified role/race/gender/alignment with random
  valid values, so a bare `-u<name>` boots to a complete, randomly-rolled hero.
- **`select_menu` must return `-1` (cancelled), not `0`,** to decline yes/no
  menus such as the new-game tutorial offer; `0` ("0 items selected") makes
  such prompts re-ask, which loops forever.
