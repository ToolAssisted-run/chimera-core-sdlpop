# chimera-core-sdlpop

[SDLPoP](https://github.com/NagyD/SDLPoP), the open-source port of Prince of
Persia, as a [Chimera](https://github.com/ToolAssisted-run/chimera) **game
core**: the game itself, stepped one game step at a time in miniBox's sandbox,
packaged as `sdlpop.chimeraCore`. It is the first core of its kind
(`"kind": "game"`, see Chimera's `docs/game-cores.md`).

**Built on upstream, unmodified but for seven one-line hooks.** SDLPoP and
SDL 2 are submodules, compiled from source into the core; the patch series
adds one call at each place the DOS game waited without saying for how long or
looked at the controls, and nothing else. The game runs on a stack of its own and hands
control back where it would have waited.

## What it is

- **Prince of Persia 1.0 (DOS)**, from the user's own data files. The package
  carries none of the game's data and cannot read SDLPoP's extracted folders:
  the 21 original DAT files are the project's **firmware**, checked file by
  file against 1.0's SHA-1 at Init. A missing one is named ("Prince of Persia
  needs KID.DAT - add it as the project's firmware"); a file of another release
  or a damaged one is refused by name.
- **A frame is one step of the game**: a tick of play (1/12 s walking, 1/10 s
  fighting), each 1/60 s of the title, the cutscenes and a pause (which poll
  the controls that often), and a dark 1/10 s step when the prince changes
  rooms, as the original does. `GetVsyncNumerator/Denominator` report the step
  just run; a step that did not read the controls is a lag frame.
- **The controls are the game's**: Up, Down, Left, Right, Shift, Enter and
  Restart Level (Ctrl+A), delivered to SDLPoP as the key events a keyboard
  would send.
- **Properties**: 183 properties in `GetGameProperties` (JaffarPlus's list,
  and more): the prince and the guard, the level, the clock, the random seed
  in a packed `Game State` block, copied out after each step and back before
  the next (so pokes and freezes work); the level's rooms, tiles, links,
  guards and button events, the falling floors, the tile animations and the
  hall of fame in place, as further domains, described as arrays and bit
  fields.
- **Settings** that change play, recorded in the project: the sound card, the
  random seed, copy protection (on, as in 1.0), the first level, the minutes,
  the hit points, skipping the title, and SDLPoP's fixes and enhancements
  (off, with each one's own switch). Defaults are the original game.
- **A custom level set** is the one file a project may add: a LEVELS.DAT
  played in place of the original levels.

## Building

```
git submodule update --init --recursive
make -C waterbox -f native.mk -j$(nproc)    # the native reference and the harnesses
make -C waterbox -f guest.mk -j$(nproc)     # core.wbx
./waterbox/build-package.sh                 # build/package/sdlpop.chimeraCore
```

miniBox is taken from `MB=`/`MINIBOX_DIR`, else `~/chimera/extern/chimera-common-minibox`;
it must be built (`build/meson-linux`, which has the C guest toolchain).

## Gates

```
./waterbox/run-gate.sh                  # the core: native == sandbox, savestates, the game's own checks
```

The data is the user's: put Prince of Persia 1.0's DAT files in
`tests/roms-local/pop10` (gitignored), or pass `-d`. Without it only the
build, the declarations and the no-data refusal run.

Status, decisions and what is open: `docs/PLAN.md`.
