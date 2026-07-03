# CLAUDE.md

This repository is the **engine fork** for the Vibe-crawler project: upstream
NetHack plus minimal patches on the `dcc-engine` branch. The product — C
driver, Node/TS game server, React client, project plan and docs — lives in
**https://github.com/mbsperry/vibe-crawler**, which pins this repo as a git
submodule at `engine/nethack`. If you were asked to work on anything other
than the engine itself, you're probably in the wrong repo.

## Rules for engine work here

- **Never touch saved structs or `EDITLEVEL`** — save files must stay
  compatible with upstream.
- Log every patch to upstream code in `docs/engine-patches.md` (what, why,
  fix, verification), one focused commit per patch.
- `NetHack-5.0` mirrors upstream NetHack — never commit to it. Patches go on
  `dcc-engine`. Upstream sync = merge `NetHack-5.0` into `dcc-engine`,
  rebuild, run Vibe-crawler's `engine/driver/smoke.sh`, then bump the
  submodule pin over there.
- Build for the driver: `git submodule update --init submodules/lua`, then
  `cd sys/unix && sh setup.sh hints/linux.500 && cd ../..`, then
  `make GIT=1 WANT_LIBNH=1 all` → `src/libnh.a`.
