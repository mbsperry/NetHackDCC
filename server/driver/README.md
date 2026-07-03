# nhdcc-driver — NetHackDCC engine driver (v0)

Boots the NetHack engine (`libnh.a`) headless, registers the shim graphics
callback, and streams every window/UI event as newline-delimited JSON (NDJSON)
on stdout. This is the Phase-0 spike proving the engine bridge end to end; it
is **not** the Phase-1 protocol (which frames both directions as NDJSON).

## Build

First build the engine library from the repo root (produces `src/libnh.a`):

```sh
git submodule update --init submodules/lua
cd sys/unix && sh setup.sh hints/linux.500 && cd ../..
make GIT=1 WANT_LIBNH=1 all
```

Then build the driver:

```sh
make -C server/driver
```

## Run

```sh
./server/driver/nhdcc-driver --session <dir> [--data <datdir>] [--name <plname>]
```

- `--session <dir>` (required): per-session playground. Created if missing;
  populated with symlinks to the data bundle (`nhdat`, `symbols`, `license`),
  empty `perm`/`record`/`logfile`, and a `save/` dir. The driver `chdir()`s
  here (this `libnh` build is compiled without `CHDIR`, so the engine won't).
- `--data <datdir>`: directory holding `nhdat` etc. Defaults to `$NHDCC_DATA`
  or `dat`. Use an absolute path if not launching from the repo root.
- `--name <plname>`: hero name (default `DCCbot`). A non-generic name avoids
  the engine's askname prompt.

Input (v0): raw keystrokes are read from `$DCC_KEYS` first, then stdin. On EOF
the run ends cleanly (emits `{"cb":"__eof"}` and exits 0), so piping input or
`< /dev/null` gives a bounded boot dump.

`$DCC_INJECT_INVENTORY` (v0, Phase-0 task 5): if set, the driver queues the
engine's `inventory` extended command (`ddoinv`, resolved from
`extcmdlist[]`) onto `cmdq_add_ec(CQ_CANNED, ...)` the first time it's about
to read a real keystroke. The engine drains that queue at the very start of
the *next* player turn (`rhack()`, `src/cmd.c`), ahead of any keypress, so
the inventory menu appears without ever sending an `i` -- the same
canned-command mechanism the engine itself uses (see `act_on_act()` at
`src/cmd.c:4688`). The driver emits a synthetic `{"cb":"__inject_inventory"}`
marker at the moment it queues the command.

### Example

```sh
# Boot to the first map and dump every event, then exit at the first prompt:
./server/driver/nhdcc-driver --session /tmp/s1 --data "$PWD/dat" < /dev/null

# Walk one step east, then stop:
DCC_KEYS=l ./server/driver/nhdcc-driver --session /tmp/s2 --data "$PWD/dat" < /dev/null
```

## Event format

Each shim callback becomes one line:

```json
{"cb":"shim_putstr","fmt":"viis","args":[4,0,"Hello DCCbot"]}
{"cb":"shim_print_glyph","w":2,"x":41,"y":4,"ch":64,"color":15,"monIdx":343}
{"cb":"shim_create_nhwindow","fmt":"ii","args":[1],"ret":1}
```

- `fmt` is the shim type string: first char is the return type, the rest are
  arg types (`i` int, `s` string, `p` pointer, `0`/`1`/`2` byte/short/int,
  `b` boolean, `c` char, `v` void). char/short/boolean/coordxy arrive
  int-promoted over varargs.
- `args` are positional: scalars as numbers, strings as JSON strings (`null`
  if NULL), pointers as `"0x…"` (raw, undereferenced -- for whichever shim
  callbacks don't get bespoke decoding below).
- `ret` (when present) is the value the driver returned to the engine.
- **`shim_print_glyph` is decoded, not passed through `fmt`/`args`** (Phase-0
  task 4): `w`/`x`/`y` are the window id and cell; `ch` is the resolved
  display character for the current symset; `color` is the engine's tty
  color index (`CLR_*`); `monIdx` is the monster index (`glyph_to_mon()` via
  the glyph-band macros in `include/display.h`) if the glyph is any monster
  band (normal/pet/ridden/detected, male or female), else `-1`. Only the
  foreground `glyphinfo` arg is decoded; the background arg (used for frame
  coloring under riders/piles) is dropped. Decoding lives in
  `glyphdecode.c`, the one translation unit in this driver that includes
  `hack.h` -- see its file comment for why it must be compiled with the
  same defines `libnh.a` was built with.
- Blocking input calls emit an ask line first (`shim_nhgetch` /
  `shim_nh_poskey`) and, once answered, a `"<name>.answer"` line with the key.
- `__boot` / `__eof` / `__exit` are driver-synthesized markers.

## Smoke test

```sh
sh server/driver/smoke.sh
```

Builds (if needed) and asserts a clean boot: exit 0, and at least one
`shim_print_glyph` (map drawn) plus a trailing input prompt. A second run
with `DCC_INJECT_INVENTORY=1` asserts the injected `inventory` extcmd fires
exactly once and produces inventory menu items.
