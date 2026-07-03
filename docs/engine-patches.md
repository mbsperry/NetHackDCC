# Engine Patches Log

Every change to the upstream engine (`src/`, `include/`, `win/`, `sys/`, `dat/`) made in service of this project, in commit order. Rule: never touch saved structs or `EDITLEVEL` — saves must stay compatible with upstream.

## 1. `sys/unix/hints/linux.500` — fix `recover` target missing `lua_support` dependency under `WANT_LIBNH`

**Problem**: building `libnh.a` on Linux (`make GIT=1 WANT_LIBNH=1 all`) failed with `fatal error: nhlua.h: No such file or directory` while compiling `util/recover.c`.

**Cause**: the `WANT_LIBNH` block overrides `GAME=` (empty), since no game executable is built in library mode. The top-level `recover: $(GAME)` dependency chain, which normally triggers the `lua_support` target (generator of `include/nhlua.h`), becomes a no-op with `$(GAME)` empty. `recover.c` still transitively includes `nhlua.h` via `hack.h`, so the build fails.

**Fix**: `sys/unix/hints/macOS.500` already carries the fix for this — an explicit `recover: lua_support` rule inside the `WANT_LIBNH` block, guarded by `ifdef MAKEFILE_TOP` so it's only added once (at the top-Makefile level, where both `recover` and `lua_support` targets live). `linux.500` was missing this rule; ported it verbatim.

**Verification**: clean rebuild (`rm -f src/libnh.a src/*.o include/nhlua.h && make GIT=1 WANT_LIBNH=1 all`) completes with exit 0, no errors, and `nm src/libnh.a` shows `nhmain`, `shim_graphics_set_callback`, and all four `cmdq_add_*` symbols exported.

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
