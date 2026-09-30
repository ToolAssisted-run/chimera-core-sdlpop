# chimera-core-sdlpop

[SDLPoP](https://github.com/NagyD/SDLPoP), the open-source port of Prince of
Persia, as a [Chimera](https://github.com/ToolAssisted-run/chimera) **game
core**: the game itself, stepped one game step at a time in miniBox's sandbox,
packaged as `sdlpop.chimeraCore`. It is the first core of its kind
(`"kind": "game"`, see Chimera's `docs/game-cores.md`).

**Built on upstream, with three small patches.** SDLPoP and SDL 2 are
submodules, compiled from source into the core. Patch 0001 adds one call at
each place the DOS game waited without saying for how long or looked at the
controls; the game runs on a stack of its own and hands control back where it
would have waited. Patch 0002 lets the core give the game its release's copy
protection (each release's manual asks other words) and its own Ctrl+V line;
patch 0003 enters the hall of fame's name from a setting.

## What it is

- **Prince of Persia (DOS), the release the project names**: 1.0, 1.1, 1.3 or
  1.4 (the `version` setting; 1.0 by default), from the user's own files. The
  package carries none of the game's data and cannot read SDLPoP's extracted
  folders: the release's DAT files and its PRINCE.EXE are the project's
  **firmware**. 1.0 and 1.1 have the same data files, and so do 1.3 and 1.4;
  PRINCE.EXE, which is never run (SDLPoP is the program), tells each pair
  apart. A missing file is named ("Prince of Persia 1.0 needs KID.DAT - add it
  as the project's firmware"). A file of your own - a modified one, another
  release's - may take an original's place: the core takes it as it is, and
  the project pins its hash.
- **What differs between the releases is played**: the guards fight by 1.0's
  tables in 1.0 and by the later ones in 1.1, 1.3 and 1.4 (read out of each
  PRINCE.EXE, and as CusPop gives them); 1.3 and 1.4 have the level colours
  (level 3's dungeon green, not blue); the copy protection asks from 1.0's,
  1.1's or 1.3/1.4's manual; Ctrl+V shows the release's own line. Of
  everything SDLPoP can read from a DOS PRINCE.EXE, nothing else differs.
- **A frame is one step of the game**: a tick of play (1/12 s walking, 1/10 s
  fighting), each 1/60 s of the title, the cutscenes and a pause (which poll
  the controls that often), and a dark 1/10 s step when the prince changes
  rooms, as the original does. `GetVsyncNumerator/Denominator` report the step
  just run; a step that did not read the controls is a lag frame.
- **The controls are the game's keyboard**, each key its own button, delivered
  to SDLPoP as the key events a keyboard would send (a Ctrl or Shift command
  holds its modifier as a key does): the prince's, P1 Up, Down, Left, Right,
  Shift and Enter - after the separator in a movie's rows
  (`|commands|P1 Up Down Left Right Shift Enter|`), with the commands and the
  cheats before it; the commands - Pause (Esc), Show Time (Space), Restart Level (Ctrl+A),
  Restart Game (Ctrl+R), Next Level (Shift+L), Sound On/Off (Ctrl+S), Version
  (Ctrl+V). Left out: Ctrl+Q, which ends the program, Joystick Mode and
  Keyboard Mode (Ctrl+J, Ctrl+K), which mean nothing to a movie, and the game's
  own saved games (Ctrl+G, Ctrl+L). There are no letter keys: the name a won game enters in the hall of
  fame is the **Player Name (Hall of Fame)** setting ("Chimera" by default),
  entered by the game itself.
- **Cheats**, with the `cheats` setting (off by default): the game starts with
  its cheat word, as from the DOS command line, and 16 more buttons exist -
  Show Rooms, Show Corner Rooms, Less Time, More Time, Revive, Kill Guard,
  Flip Screen, Feather Fall, Look Left/Right/Up/Down, Look Back, Blind Mode,
  Add Hit Point and Add Max Hit Point. Without the setting they are not
  buttons at all (`IsButtonActive`).
- **Properties**: 193 in `GetGameProperties` (JaffarPlus's list, and more): the
  prince and the guard, the level, the clock, the random seed in a packed
  `Game State` block, copied out after each step and back before the next (so
  pokes and freezes work); the level's rooms, tiles, links, guards and button
  events, the falling floors, the tile animations, the hall of fame, and the
  guards' fighting tables and level colours the game plays by, in place, as
  further domains, described as arrays and bit fields.
- **The game's timer**: `Time.IGT Ticks` and `Time.IGT Ms`, TASVideos'
  GameTimer as quickerSDLPoP prints it - the ticks of 1/12 s between the clock
  the game started with and the clock now (it loses one per tick of play and
  rolls a minute over after 719), times 1000/12. The table names it
  (`"gameTimer"`), so Chimera shows it as `IGT mm:ss.mmm` and saves it in the
  project at the end of the movie.
- **Settings** that change play, recorded in the project: the version, the
  cheats, the player name, the sound card, the random seed, copy protection (on, as in the
  original), the first level, the minutes, the hit points, skipping the title,
  and SDLPoP's fixes and enhancements. **Defaults are the original game**:
  every fix and enhancement is off, each one's own switch as well as the
  master switch.
- **Files a project may add**: a custom level set (a LEVELS.DAT played in
  place of the original levels) and a **savestate** - an SDLPoP quicksave
  (QUICKSAVE.SAV) the run starts from. The files the game writes itself (its
  hall of fame) live in guest memory, so a savestate carries them.

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
./waterbox/tests/run-frontend.sh        # the package in Chimera: chimera-run and the headless frontend
```

The frontend gate uses a built Chimera checkout (`--chimera-root`, else
`../chimera` or `~/chimera`) and writes nothing into it: its config, data
home and Base path are all in `build/frontend`.

The data is the user's: put each release's files (its DAT files and
PRINCE.EXE) in `tests/roms-local/pop10`, `pop11`, `pop13` and `pop14`
(gitignored), or pass `-d` with the folder holding them. Without 1.0's only
the build, the declarations and the no-data refusal run; without the others
the releases' legs are skipped.

Status, decisions and what is open: `docs/PLAN.md`.
