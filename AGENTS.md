# AGENTS.md - SDLPoP core for Chimera

This repository builds SDLPoP, the open-source port of Prince of Persia, as a
game core for Chimera (https://github.com/ToolAssisted-run/chimera), a
frontend for tool-assisted speedruns. It is a game core: one game rebuilt as
a core and stepped one game step at a time, not an emulator. It produces one
file, `sdlpop.chimeraCore`, which Chimera's sandbox (miniBox) runs on Linux
and on Windows. The package carries none of the game's data.

## Layout

- `extern/SDLPoP` - submodule: the game (upstream SDLPoP).
- `extern/SDL` - submodule: SDL 2, compiled into the core unpatched.
- `patches/` - the numbered patches applied to `extern/SDLPoP`.
- `waterbox/sdlpop-driver.c`, `game-state.c`, `seams.c`, `coroutine.c`,
  `wbx-entry.c` - the core itself.
- `waterbox/settings.inc` - SDLPoP's options as settings.
- `waterbox/gen-config.py` - writes `waterbox/waterbox.config`.
- `waterbox/waterbox.config` - what Chimera is told: kind, releases, settings,
  buttons, the firmware list. Generated.
- `waterbox/file_slots.json`, `default_keybinds.json`,
  `package-licenses.json` - packed as they are.
- `waterbox/sources.mk`, `native.mk`, `guest.mk` - the build.
- `waterbox/apply-patches.sh`, `build-package.sh`, `run-gate.sh` - scripts.
- `waterbox/run-native.c`, `run-wbx.c`, `gate-harness.h` - the gate's two
  harnesses.
- `waterbox/tests/` - the frontend gate (`run-frontend.sh`), the gate's
  checkers and its route.
- `tests/roms-local/` - the game's files for the gates. Gitignored.
- `build/` - everything built. Gitignored.
- `docs/BUILDING.md` - the build in detail. `docs/PLAN.md` - decisions, log.
- `.github/workflows/chimera.yml` - CI: the authoritative build recipe.

## Set up the build environment

On Ubuntu, as CI does:

```
sudo apt-get update
sudo apt-get install -y --no-install-recommends meson ninja-build build-essential cmake pkg-config python3 mono-complete xvfb libgl1-mesa-dev libegl-dev libx11-dev libxext-dev libasound2-dev
```

The sources: this repository with its submodules, and a Chimera checkout with
its submodules (`<chimera>` below).

```
git submodule update --init --recursive
git clone --recursive https://github.com/ToolAssisted-run/chimera.git <chimera>
```

miniBox, the sandbox host and the guest toolchain, is Chimera's submodule
`extern/chimera-common-minibox`. Build it once:

```
export MINIBOX_DIR=<chimera>/extern/chimera-common-minibox
[ -f "$MINIBOX_DIR/build/meson-linux/build.ninja" ] || meson setup "$MINIBOX_DIR/build/meson-linux" "$MINIBOX_DIR"
meson compile -C "$MINIBOX_DIR/build/meson-linux"
```

Without `MINIBOX_DIR` (or `-m <dir>`, or `MB=<dir>` for make) the scripts look
in `~/chimera/extern/chimera-common-minibox`.

## Build

```
make -C waterbox -f native.mk -j"$(nproc)"    # build/native/run-native, run-wbx
make -C waterbox -f guest.mk -j"$(nproc)"     # build/guest/core.wbx
./waterbox/build-package.sh -r <chimera>      # <chimera>/build/Cores/sdlpop.chimeraCore
```

- The makefiles apply `patches/` to `extern/SDLPoP` themselves.
- `build-package.sh` runs `guest.mk` again, so the first two lines can be
  replaced by `./waterbox/run-gate.sh`, which runs both. Do not run
  `build-package.sh` first in a fresh clone: it writes a log into `build/`
  before that directory exists.
- `build-package.sh -o <dir>` writes to `<dir>` instead; with neither option
  the package is `build/package/sdlpop.chimeraCore`.
- A hand-built package is stamped `<commit>+local` (`-dirty` when tracked
  files differ from HEAD). It is for testing. CI stamps the commit.

## Install the core into Chimera

Chimera includes no cores and downloads nothing. A core is a file in its
`Cores` folder.

- Source checkout: `<chimera>/build/Cores/`. `build-package.sh -r <chimera>`
  writes there.
- Release bundle: the `Cores` folder beside `Chimera.exe`, or the folder
  chosen in File > Core Manager > Change folder...
- File > Core Manager lists the folder; Refresh List rescans it.

## Test before you commit

```
./waterbox/run-gate.sh                                        # the core gate
./waterbox/tests/run-frontend.sh --chimera-root <chimera>     # the package in Chimera
```

- Both must end with `0 failed`. `run-gate.sh` exits non-zero otherwise.
- The game legs need the user's files in `tests/roms-local/pop10`, `pop11`,
  `pop13` and `pop14` (each release's DAT files and PRINCE.EXE). Without
  `pop10` the gate runs only the build, the declarations and the no-data
  refusal, and the frontend gate runs nothing. That is all CI can run. A
  change to the game's behaviour is not tested until the gate has run with
  the files.
- `run-gate.sh -q` skips the build; the `build:fresh` leg fails if a source
  is newer than `core.wbx`.
- The frontend gate needs a built Chimera (`build/Chimera.exe`,
  `build/dll/chimera-run`), `mono` and `Xvfb`. It skips what it cannot run.
- Results and pictures: `build/gate/`, `build/frontend/`.

CI also runs Chimera's contract tests against the package
(`docs/BUILDING.md`, "Chimera's contract tests").

## Rules of this repository

- `extern/SDLPoP` and `extern/SDL` are submodules. A change to SDLPoP is a
  numbered patch in `patches/`, under `#ifdef CHIMERA_CORE`, applied by
  `waterbox/apply-patches.sh`. Never commit inside a submodule. After a build
  `git status` shows `extern/SDLPoP` modified: that is the applied series.
- When the SDLPoP pin moves, rebase the patches. `apply-patches.sh` stops if
  the series does not apply to the submodule's HEAD.
- Determinism is the product. The guest must not read host time, host
  randomness or anything else that differs between runs (`seams.c` answers
  those calls). A savestate must round-trip. The gate checks both; a change
  that breaks either is a bug.
- `waterbox/waterbox.config` is generated. Change `settings.inc`, the driver
  or `gen-config.py`, then run `python3 waterbox/gen-config.py`. The gate's
  `wire:config==driver` leg fails when they disagree.
- Defaults are the original game: every SDLPoP fix and enhancement is off.
  The gate's `settings:original-defaults` leg holds this.
- Run the gate before committing. A new leg needs a negative control: break
  the thing it checks, see the leg fail, revert.
- Never commit the game's files. Never add network access.
- Shell scripts stay executable (git mode 100755). Docs are plain ASCII.
- Commit subjects say what is now true, most with a prefix: `feat:`,
  `fix(input):`, `test(gate):`, `build:`, `ci:`, `input:`. The body says in
  prose what changed and what was measured (gate counts, the negative
  control).
- Do not edit `.github/workflows` unless the task is the workflow.

## Where to read more

- `docs/BUILDING.md` - requirements, every script option, the files the core
  needs, troubleshooting.
- `docs/PLAN.md` - why the core is built as it is, and what is open.
- `README.md` - what the core is: releases, controls, settings, properties.
- In the Chimera checkout: `docs/game-cores.md`, `docs/porting-a-core.md`,
  `docs/core-manager.md`, `docs/gates.md`.
