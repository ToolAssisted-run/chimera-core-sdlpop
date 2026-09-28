# SDLPoP as a Chimera game core: plan and log

## What this is

Prince of Persia 1.0 (DOS), played by SDLPoP - David Nagy's port of the game
from its disassembly - as a Chimera core. The first **game core**
(Chimera's `docs/game-cores.md`, user-decided 2026-09-28): a core that is one
game rather than one machine, whose data files are firmware, whose options
that change play are settings, whose movie rows are steps of the game, and
whose tools watch the game's own properties by name.

Upstream is two submodules, both compiled from source into the core:

- `extern/SDLPoP` - NagyD/SDLPoP, pinned; one patch (seven one-line hooks).
- `extern/SDL` - libsdl-org/SDL at release-2.32.10, unpatched.

The core's own code is `waterbox/`: the driver (`sdlpop-driver.c`), the
seams where SDLPoP reaches outside itself (`seams.c`), the game's stack
(`coroutine.c`), the property table (`game-state.c`) and the guest ABI
(`wbx-entry.c`).

## SDL: the real one, built small (decided 2026-09-28)

SDLPoP draws with SDL 2 surfaces: 8-bit paletted sprites with colour keys and
alpha modulation blitted onto a 24-bit screen, ARGB8888 blends for coloured
text, conversions, fills. Two ways were open: replace the SDL calls with a
shim of our own, or build SDL for the guest.

A shim would have been about a hundred functions, most trivial, and a few
hundred lines of blitting whose exact semantics (which blitter SDL picks for
which combination of formats, keys and blend modes) SDLPoP was written
against. Getting those wrong changes nothing the game computes, but changes
the picture, and nothing would say so: the native reference would share the
mistake.

So the core builds **SDL 2 itself**, the way SDL's own `Makefile.minimal`
does: every subsystem's portable C with `include/SDL_config_minimal.h` - the
dummy video and audio drivers, no threads, no timers, no joysticks, no
dynamic loading. On top (`waterbox/sources.mk`):

- `SDL_CPUINFO_DISABLED`: SDL asks the CPU nothing, so every blitter is the C
  one on every host - the picture cannot depend on the machine drawing it;
- `DYNAPI_NEEDS_DLOPEN` without `HAVE_DLOPEN`: SDL's dynamic API is off, which
  would otherwise let an environment variable load another SDL;
- `HAVE_MALLOC`, `HAVE_STDIO_H`: SDL allocates and opens files with the C
  library, like the game, rather than with a dlmalloc of its own.

No SDL_image: SDLPoP only loads PNGs from its extracted data folders, its
window icon and the lighting mask. A header declares its three functions and
`seams.c` answers "no" - which also means a PNG can never stand in for a DAT.

What the core answers itself is linked with `-Wl,--wrap` (both builds, one
list in `sources.mk`): the performance counter, the tick count and
`SDL_Delay` (the clock), `SDL_Init` (asked for video and events only - timers
and game controllers need threads), the audio device, presenting a frame,
`SDL_PollEvent` (the safety net below), and from the C library `fopen`,
`access`, `stat`, `opendir`, `mkdir`, `time` and `exit`. The game sees the
DAT files the project mounted and an `SDLPoP.ini` the core writes from the
settings, and nothing else; it writes nothing (saves, the hall of fame,
screenshots and replays fail as on a read-only disk). That is also what makes
the native reference see what the sandbox sees - a user's `~/.SDLPoP` or a
`PRINCE.EXE` beside the data cannot reach either.

## The game on its own stack

SDLPoP's main loop is the DOS game's: it runs the game and waits for timers
inside that loop. A core is called one step at a time. So the game runs on a
stack of its own (`coroutine.c`: the System V callee-saved registers, the
MXCSR and the x87 control word, swapped by fifteen instructions), and each
place it would have waited switches back to `FrameAdvance`; the next
`FrameAdvance` switches in again. Nothing is threaded.

The stack is 4 MiB asked for with `MAP_STACK`, which miniBox needs to know
(a write fault on the page the stack pointer is in cannot be reported the
ordinary way on Windows - chimera-core-ares found it). Everything a suspended
game holds is on that stack and in its heap, so a savestate between two steps
carries the whole suspended game: the gate loads one into a new host half way
through a run and finishes the run there.

## A frame is a step of the game

The clock counts 1,764,000 a second (44100 x 40), so a sample, a
millisecond, a tick of the game's 60 Hz timer and a step of its 120 Hz screen
transition are whole numbers. It moves only where the game waits. A step
lasts:

- **to the timer's deadline**, when `do_simple_wait` waits for a game timer
  without looking at the controls (the hook `chimera_wait_for_timer`). That is
  a tick of play: 12 a second walking, 10 fighting. The whole tick is one
  step, not five 1/60 s ones.
- **otherwise to the next tick of the 60 Hz clock at least, and at least as
  long as the delay asked for.** Every other wait in the game polls the
  controls while it waits - the title's cards, the cutscenes, a pause, the
  wait for a sound to end - so each 1/60 s of it is a step that reads them.
  The 120 Hz screen transition runs two of its steps in each, through its own
  catch-up logic.
- **1/60 s per pass** for the two loops that wait for a key without letting
  any time pass at all (a message box, the Hall of Fame's name entry) and the
  one that waits for a sound without a delay (`play_level`'s wait before a
  level starts) - the hook `chimera_idle_step`.

What that gives, measured: play steps at 12/1; a room change is a 10/1 step
with the screen dark (SDLPoP's `USE_DARK_TRANSITION` delay, which is the
original's look) and the next tick is 15/1, short by as much, so two ticks
still take 10 units; a hurt flash splits its tick in two, the second part a
lag step; the title's fades step at 30/1 and its cards at 60/1.

`GetVsyncNumerator/Denominator` report the step just run, and 12/1 before the
first (the rate of play). **The engine reads them once, after Init**
(`session.cpp:1411-1418`), so today Chimera runs the whole game at 12 steps a
second - the title's 60 Hz steps play five times too slowly. For the rate to
follow the game, the engine has to read it after every FrameAdvance
(docs/game-cores.md already says "the rate of steps now").

**A safety net under the hooks**: a loop that keeps pumping events without
the clock moving would spin forever (SDLPoP spins such loops at full speed and
real time ends them). After 64 empty pumps the loop is given a 1/60 s step and
the core says so once on stderr. It is how the third loop above was found: the
route into level 2 hung before the hook, and ran with the net alone, to the
same pictures.

## Input, sound and the picture

**Input** is what a keyboard would send: `FrameAdvance` turns the buttons that
changed into SDL key events (Shift and the Ctrl of Ctrl+A as real modifiers),
and the game's own `process_events` takes them the moment its wait is over.
So the "any key" checks, Shift starting a game, Enter restarting after death
and Ctrl+A restarting the level are upstream's. `InputWasRead` is set by three
hooks: where the game reads the keyboard, the joystick, or the last key.

**Sound** is pulled, not pushed. At the end of each step the callback SDLPoP
gave `SDL_OpenAudio` is run for exactly the samples the step covers, in
1024-sample blocks as SDL's device would ask for them, and only while the
device is unpaused. The game waits for sounds to end (a level's closing
music, the opening music's crouch on level 1, the title), so sound is part of
the machine here, not only its output: the sound card is a setting.

**The picture** is what SDLPoP hands `SDL_UpdateTexture`: its 320x200 screen
as presented, copied at that moment. Turbo skips only the conversion to BGRA;
the game draws whatever happens (its drawing is part of the game).

## The data

The 21 DAT files of Prince of Persia 1.0 are firmware, declared under their
own names with 1.0's size and SHA-1. Always needed: PRINCE, KID, LEVELS,
TITLE, PV, VDUNGEON, VPALACE, GUARD, GUARD1, GUARD2, FAT, SKEL, VIZIER,
SHADOW, IBM_SND1, IBM_SND2 (SDLPoP opens the PC speaker files whatever the
card). With the Sound Blaster (the default): DIGISND1-3 and MIDISND1-2. Not
needed: PRINCE.EXE, SETUP.*, CONFIG.DAT, the drivers.

Init hashes every file the game will open. A missing one is named; one of
another release is refused by name - SDLPoP's repository ships its own
DIGISND/MIDISND files, a later release's (1.3-format waves), and those five
hashes are recognised; anything else is refused with both hashes. **What 1.3
and 1.4's other files hash to is not known here**, so they are refused as
"not 1.0's" rather than by release. Note that Chimera's own firmware check
does not stop a wrong file reaching the core: the wizard refuses one, but a
`--firmware` file is not hash-checked and a mismatch at boot is only a
warning - which is why the core checks.

## Settings

56, all things that change play, recorded in the project:

- `sound` (digital / pcSpeaker) and `random_seed` (the DOS game took it from
  the clock; SDLPoP's own `seed=` argument);
- `enable_copyprot` (**on**: 1.0 has the potions level after level 1, and
  SDLPoP's own default is off), `first_level`, `skip_title`,
  `start_minutes_left`, `start_hitp`, `max_hitp_allowed`;
- `use_fixes_and_enhancements` (off) and each of SDLPoP's 48 fixes and
  enhancements under its own ini name (each on, as in SDLPoP.ini; none acts
  until the master switch is on). `fix_drop_2_rooms_climbing_loose_tile` is
  missing: SDLPoP's ini reader does not read it, so it is always on with the
  master switch.

`settings.inc` is the one list; `gen-config.py` writes `waterbox.config` from
it and the driver's data-file table, and `tests/check-wire.py` holds the
three together.

Pinned, not settings: no SDLPoP info screen, no pause menu, no quicksaves or
replays (keys the panel does not have, files the sandbox never writes), no
lighting, sharp scaling, and the original's fades, flashes and texts - which
change how many steps a scene takes, so are not picture-only either, and stay
at the original.

## Properties

`GetGameProperties` exports 183 properties (100 arrays, 3,979 values):

- **Game State**, 139 bytes: the prince and the guard (position, room, frame,
  action, direction, hit points, sword...), the level (current, next, drawn
  room, door, checkpoint...), the game's clock (minutes, ticks), the random
  seed, sounds, effects, collisions - JaffarPlus's list
  (`games/sdlpop/princeOfPersia.hpp`), with enum names where the game has
  them. Derived values are `writable: false`: the level being played (poke
  Level.Next instead), JaffarPlus's interpolated `Kid.Pos X/Y` (f32) and
  previous frame, and the core's clock (u64).
- **Level** (SDLPoP's `level`, 2,305 bytes, in place): each room's 30 tiles
  (the type as a 5-bit field with the tile names, the flag bit above it, the
  modifier), the room links and room guards as 24-element arrays, the 256
  button events as bit fields (tile, room split over two bytes, last, timer),
  the start room, tile and direction.
- **Mobs** (falling floors), **Trobs** (tile animations) and **Hall of Fame**
  (names as strings) in place.

Rooms keep the game's numbers (element i of a room table is room i+1) and
tiles its positions 0-29, so `Room 5.Tile[12]` is what the game calls tile 12
of room 5. `tests/check-properties.py` holds the table to the spec, bit by
bit.

## The project's one file: a level set

Chimera's wizard will not make a project for a core whose `file_slots.json`
declares no slot. The spec already names what a game core's slot is for ("a
mod or a custom level set"), so there is one: `levels`, a LEVELS.DAT played
in place of the original (0 or 1 file, not held to 1.0's hash; the original
LEVELS.DAT is then not required - `requiredWhen: {"not": {"slot": "levels"}}`).
A whole SDLPoP mod folder is not supported.

## The patch

`patches/0001` adds, under `#ifdef CHIMERA_CORE`: the include of
`chimera-hooks.h` in `common.h`; `chimera_wait_for_timer` in
`do_simple_wait`; `chimera_input_read` in `read_key`, `read_keyb_control` and
`read_joyst_control`; `chimera_idle_step` in `showmessage`, `input_str` and
`play_level`'s wait for a sound. Upstream's own build is unchanged.

## The gate (waterbox/run-gate.sh)

57 legs, all green on 2026-09-28 with the user's 1.0 data:

- build (native, harnesses, core.wbx through check-wbx) and freshness (the
  tested core.wbx must be newer than every source);
- the declarations against the driver (buttons, settings, data files);
- refusals: no data at all, a missing KID.DAT, SDLPoP's later-release
  DIGISND1.DAT, a LEVELS.DAT with one byte changed; the PC speaker needs no
  DIGISND/MIDISND;
- five runs - the title into a game (700 steps), JaffarPlus's level 1 route
  on into level 2 (1100), the same route with copy protection (1150), the PC
  speaker (500), the fixes on (420) - each: sandbox twice (determinism),
  native == sandbox (every digest and every step's properties), input shapes
  the game, turbo, a savestate round trip before every step, a new host
  from a state half way;
- five pictures, each looked at: the title's "presents" card, level 1, the
  princess's cutscene, level 2, the potions level's manual question
  (`build/gate/*.png`);
- the route leaves level 1 on the step it should, goes on to level 2, and
  with copy protection to the potions level;
- the rates (12/1, 10/1 then 15/1, 30/1, 60/1, and 10/1 with the sword
  drawn) and the lag (none in play; the title's one, a fade ending);
- the property table, a poke (Kid.X, Level.Next - the game goes to level 3), a
  poke in place (the floor under the prince, a bit field - he falls), a
  freeze (the clock's ticks: 60 minutes after 800 steps, 59 without);
- the level-set slot, and one named but missing;
- the settings reaching the game (level 3, 5 minutes, 5 hit points), the PC
  speaker sounding otherwise;
- the package, the same twice.

**Negative controls**, each break alone, the gate rebuilt, then reverted:

| Break | Legs that failed |
| --- | --- |
| the guest seeds differently from the native build | 5 equivalence, a picture |
| the seed from rdtsc | determinism and equivalence of every run |
| FrameAdvance ignores the buttons | input-shaped, the route, the pictures after input |
| turbo also drops the step's sound | 5 turbo |
| the driver's state in invisible memory | 5 session |
| a savestate of the previous step loaded at step 300 (harness) | 5 savestate |
| red and blue swapped | 3 pictures (then 5) |
| copy protection forced off | copy protection, its picture |
| do_simple_wait's hook a no-op | rates, lag, route, poke, freeze |
| the fighting speed ignored (fight_speed read as base_speed) | fight-rate |
| the input-read hook a no-op | lag |
| a property named twice / overlapping / bits shared / a bad type / past its domain | the table checker, each by name |
| no copy back into the game | poke, freeze |
| the Level domain a copy | poke in place |
| settings ignored / the sound setting ignored | settings / sound card, speaker-needs-less |
| no file check | 4 refusals |
| the level-set slot ignored / its file not checked | slot:levels / slot:levels-missing |
| a date in build.json | package |
| a button renamed in waterbox.config | wire |
| a row dropped from the route | the route legs |
| a source touched, gate run with -q | build:fresh |
| play_level's hook removed | nothing - the safety net stepped it (and said so) |
| the hook and the safety net removed | the route hangs; the gate's timeout fails it |

The first run of the controls was itself wrong: restoring a file from its
backup gave it the backup's older time, so make kept the broken objects and
each break leaked into the next. Restores are touched now.

## The frontend gate (waterbox/tests/run-frontend.sh)

9 legs, green on 2026-09-28 against Chimera 3f504a61d, each seen to fail
with its own break:

- **chimera-run**, the engine with no frontend: the route from a movie file,
  and from a hand-written project with no files (the settings and the movie
  from the project) - the Game State and Level domains and the last picture
  are the native reference's; a project whose "levels" slot holds a level set
  with one tile changed plays it without LEVELS.DAT.
- **Chimera itself**, headless under Mono on a private Xvfb, with a config,
  data home and Base path of its own (the gate checks nothing was written
  under the checkout): the project opened and played through Lua, its
  domains the reference's; the property library - `game.list()` has the
  core's 183, `game.get("Kid.X")` is the block's byte, the bit-field element
  `Room 1.Tile[10]` reads, `game.set` takes, an unknown name is nil; the
  core's own refusal (SDLPoP's later-release DIGISND1.DAT, in a project that
  pins nothing so Chimera asks nothing first) is what Chimera shows; the
  package's keybinds become the defaults.

| Break | Leg that failed |
| --- | --- |
| the guest seeds differently | engine:route, engine:project, gui:project |
| the level-set slot ignored | engine:levels-slot |
| GetGameProperties empty | gui:game-properties |
| no file check | gui:refusal |
| no default_keybinds.json in the package | gui:keybinds |

(The last leg's own control - letting Chimera write where it likes - would
write into the checkout, so it was not run.)

## The known route

JaffarPlus's level 1 any% solution (`extern/quickerSDLPoP/tests/
lvl01.anyPercent.sol` in jaffarPlus, 244 rows, found with quickerSDLPoP and
1.4 data) does not replay as it is: quickerSDLPoP counts the level 1
opening-music crouch down from a fixed number (33 ticks, 4 after a restart),
where SDLPoP waits for the music itself. With 30 empty rows after row 17 -
the music's length on the 1.0 data after the route's restart at row 8 - the
rest is the solution as it was, and the prince leaves level 1 on its last row,
step 273 (`waterbox/tests/lvl01-route.txt`, run with skip_title, copy
protection off and the seed poked to 0 before step 0, as JaffarPlus sets it;
the route does not depend on the seed and finishes without the poke too). JaffarPlus's
`lvl01.state` is exactly the state Init leaves (Kid at 73,58, frame 102, four
tile animations), so step 0 is its row 1.

## Open

- **The engine reads the step rate once** (above).
- **Winning the game asks for a name** in the Hall of Fame, and the panel has
  no letter keys: the name entry cannot be finished, so a won game stays on
  it. A movie ends at the win; a player cannot go on to the title.
- SDLPoP's pause menu, cheats, Shift+L, quicksaves and replays are out of
  reach (their keys are not on the panel; the menu would change settings).
- Linux only; the Windows build and CI (`chimera.yml`) are not written.
- 1.3/1.4 hashes, beyond SDLPoP's sound files (above).
