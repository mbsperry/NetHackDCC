# Engine Patches Log

This repository is the **engine fork** for the Vibe-crawler project
(https://github.com/mbsperry/vibe-crawler): upstream NetHack plus the minimal
patches below, on the `dcc-engine` branch. The product code (driver, server,
web client) lives in Vibe-crawler, which pins this repo as a git submodule.

Every change to upstream code is logged here, in commit order.
**Rule: never touch saved structs or `EDITLEVEL`** — saves must stay
compatible with upstream. `NetHack-5.0` mirrors upstream; sync = merge it into
`dcc-engine`, rebuild, run Vibe-crawler's driver smoke test, bump the pin.

Both current patches are generic Linux build fixes — candidates for an
upstream PR to NetHack/NetHack.

## 1. `sys/unix/hints/linux.500` — fix `recover` target missing `lua_support` dependency under `WANT_LIBNH`

**Problem**: building `libnh.a` on Linux (`make GIT=1 WANT_LIBNH=1 all`) failed with `fatal error: nhlua.h: No such file or directory` while compiling `util/recover.c`.

**Cause**: the `WANT_LIBNH` block overrides `GAME=` (empty), since no game executable is built in library mode. The top-level `recover: $(GAME)` dependency chain, which normally triggers the `lua_support` target (generator of `include/nhlua.h`), becomes a no-op with `$(GAME)` empty. `recover.c` still transitively includes `nhlua.h` via `hack.h`, so the build fails.

**Fix**: `sys/unix/hints/macOS.500` already carries the fix for this — an explicit `recover: lua_support` rule inside the `WANT_LIBNH` block, guarded by `ifdef MAKEFILE_TOP` so it's only added once (at the top-Makefile level, where both `recover` and `lua_support` targets live). `linux.500` was missing this rule; ported it verbatim.

**Verification**: clean rebuild (`rm -f src/libnh.a src/*.o include/nhlua.h && make GIT=1 WANT_LIBNH=1 all`) completes with exit 0 and no errors.

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

**Verification**: `nm src/libnh.a` lists `dist2`/`eos`/`sgn` as defined (`T`)
symbols, and Vibe-crawler's `engine/driver/nhdcc-driver` links and boots the
engine.

## Build notes (not engine patches, but required and undocumented upstream)

- **`submodules/lua` must be initialized** before building: `git submodule update --init submodules/lua`.
- **`GIT=1` must be passed on the `make` command line** (in addition to `WANT_LIBNH=1`) to opt into the submodule-based Lua build. Without it, `GITSUBMODULES` is never set (it's gated on `ifeq "$(GIT)" "1"` / `ifeq "$(git)" "1"` in the generated Makefile, not on submodule presence), and the build falls back to the tarball-fetch Lua path (`lib/lua-5.4.8/...`), which fails with "Please do 'make fetch-lua'" since no tarball was fetched. `sys/libnh/README.md`'s quick-start doesn't mention this flag — worth a doc fix upstream.
- Full working build sequence on Linux:
  ```
  git submodule update --init submodules/lua
  cd sys/unix && sh setup.sh hints/linux.500 && cd ../..
  make GIT=1 WANT_LIBNH=1 all
  ```
  Output: `src/libnh.a`.

Host-side runtime obligations (CHDIR off, `SYSCF_FILE` at a compiled absolute
path, required lock/score files, `select_menu` cancel semantics) are documented
with the driver: `engine/driver/README.md` in Vibe-crawler.
