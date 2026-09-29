# SDLPoP as a Chimera game core: plan and log

## What this is

Prince of Persia (DOS) - 1.0, 1.1, 1.3 or 1.4 - played by SDLPoP - David Nagy's port of the game
from its disassembly - as a Chimera core. The first **game core**
(Chimera's `docs/game-cores.md`, user-decided 2026-09-28): a core that is one
game rather than one machine, whose data files are firmware, whose options
that change play are settings, whose movie rows are steps of the game, and
whose tools watch the game's own properties by name.

Upstream is two submodules, both compiled from source into the core:

- `extern/SDLPoP` - NagyD/SDLPoP, pinned; two patches (seven one-line hooks;
  the release's copy protection and Ctrl+V line).
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

Each release's files are firmware, declared under their own names with the
release's size and SHA-1, and required by the `version` setting (variants of
one file share its id, with disjoint `requiredWhen`s). Always needed: PRINCE,
KID, LEVELS, TITLE, PV, VDUNGEON, VPALACE, GUARD, GUARD1, GUARD2, FAT, SKEL,
VIZIER, SHADOW, IBM_SND1, IBM_SND2 (SDLPoP opens the PC speaker files whatever
the card) and PRINCE.EXE. With the Sound Blaster (the default): DIGISND1-3 and
MIDISND1-2. Not needed: SETUP.*, CONFIG.DAT, the drivers, the CGA/EGA files.

The user's four releases (2026-09-29): 1.0, 1.1 and 1.3 from the 3.5" disk
zips in Documents\TAS\roms\dos\pop1versions, 1.4 from the Collection CD
(Desktop\prince1). **1.0 and 1.1 have byte-identical data files, and so do
1.3 and 1.4**; only PRINCE.EXE tells a pair apart, so it is required too - the
core only hashes it. Every data file differs between the pairs except KID,
LEVELS, PV, GUARD*, FAT, VIZIER, SHADOW and IBM_SND*.

**Correction:** until 2026-09-29 the core called the files it had "1.0's".
They were 1.4's (the prince1 folder: PRINCE.EXE of 110855 bytes, SDLPoP's
`dos_14_packed`, "PRINCE OF PERSIA  V1.4" and the cheat word "improved" in
it, and 1.4's PRESETS.DEF/SNDDRVRS layout). The "later release's" sound files
the core refused as SDLPoP's were in fact 1.0's; only DIGISND1.DAT of
SDLPoP's repository is no release's, and it is the one still refused.

Init hashes every file the game will open. A missing one is named; one of
another release is named as that release's ("PRINCE.DAT is Prince of Persia
1.3/1.4's, not 1.0's - the project plays 1.0 (the version setting)");
anything else is refused with both hashes. Chimera's own firmware check does
not stop a wrong file reaching the core (a `--firmware` file is not
hash-checked), which is why the core checks.

## The releases (2026-09-29)

What differs between the releases in the program, read out of each PRINCE.EXE
(EXEPACK, unpacked with a small script in the session scratchpad) and checked
against SDLPoP's own EXE reader (`load_dos_exe_modifications`, whose 97
options were compared across the unpacked 1.0, 1.3 and 1.4: only the four
guard tables below differ):

- **The guards' fighting tables.** 1.0: strike 61/100/61/61/61/40/100/220/0/48/
  32/48, restrike ... 16 8 ..., impaired block 0/61/61/..., refractory
  16/16/16/16/8/.... **1.1, 1.3 and 1.4**: 75/.../50/.../60/40/60, 20 10,
  0/75/75/..., 20/20/20/20/10/.... Block, advance and extra strength are the
  same. CusPop labels the sets "1.0" and "1.3, 1.4" and says nothing of 1.1;
  1.1's PRINCE.EXE holds the later set, and the core follows the program.
- **The level colours** (tbl_level_color, 0 0 0 1 0 0 0 1 2 2 0 0 3 3 4 0): in
  the 1.3 and 1.4 EXEs only, and only 1.3/1.4's PRINCE.DAT has the palettes
  (resource 20). 1.0 and 1.1: none - level 3's dungeon is blue, not green.
- **The copy protection**: three manuals. 1.0 (the letters
  AABBCCDDEFFGHHIIJJKLLMMNOOPPRRSSTTUUVYWY, SDLPoP's tables), 1.1 (its 6-page
  manual: other pages, lines and words, letters ...VWYY) and 1.3/1.4
  (WOESPBYSKJTBCFESKMMTPYKCGSULJCDILTTAMCSG, as quickerSDLPoP has them). 1.1
  and later print "PAGE %d LINE %d WORD %d" on screen, 1.0 "WORD %d LINE %d
  PAGE %d"; the long question is the same in all.
- **Ctrl+V**: "PRINCE OF PERSIA  V1.x", each release's own.

The guard tables and level colours go to SDLPoP through its ini ([Skill N],
[Level N]); patch 0002 makes the copy protection's four tables writable, and
asks the core for the on-screen order and the Ctrl+V line. The tables the game
plays by are properties (the "Custom Options" domain: Guard Skills.* and
Level Colours), so a movie's reader sees them and the gate reads them.

## Settings

58, all things that change play, recorded in the project:

- `version` (1.0 / 1.1 / 1.3 / 1.4, **1.0**), `cheats` (off: the cheat word on
  the DOS command line, "megahit"), `sound` (digital / pcSpeaker) and
  `random_seed` (the DOS game took it from the clock; SDLPoP's own `seed=`
  argument);
- `enable_copyprot` (**on**: the original has the potions level after level
  1, and SDLPoP's own default is off), `first_level`, `skip_title`,
  `start_minutes_left`, `start_hitp`, `max_hitp_allowed`;
- `use_fixes_and_enhancements` and each of SDLPoP's 47 fixes and enhancements
  under its own ini name - **all off** (user-decided 2026-09-29: the defaults
  are the original game, with nothing of SDLPoP's; until then each fix
  defaulted on and waited for the master switch). `fix_drop_2_rooms_climbing_loose_tile` is
  missing: SDLPoP's ini reader does not read it, so it is always on with the
  master switch.

`settings.inc` is the one list; `gen-config.py` writes `waterbox.config` from
it and the driver's data-file table, and `tests/check-wire.py` holds the
three together.

Pinned, not settings: no SDLPoP info screen, no pause menu, no replays, no
quicksaves (but for the savestate slot, below; F6 and F9 are on no button),
no lighting, sharp scaling, and the original's fades, flashes and texts -
which change how many steps a scene takes, so are not picture-only either, and
stay at the original.

**`skip_title` has a trap of SDLPoP's own**: it starts the first level without
naming a level to start (`start_level` stays -1), so the first key of the run
is the title's "any key starts a game" and restarts the level. The known route
relies on it (its Shift on row 2 is that restart), so it is left as SDLPoP
has it; the savestate slot names a level instead, as the DOS game's
"prince 5" did.

## The controls (2026-09-29)

Each key the game reads is its own button (the wire order is the driver's
`PopButton`): Up, Down, Left, Right, Shift, Enter; the commands Pause (Esc),
Show Time (Space), Restart Level (Ctrl+A), Restart Game (Ctrl+R), Next Level
(Shift+L), Sound On/Off (Ctrl+S), Version (Ctrl+V), Joystick Mode (Ctrl+J),
Keyboard Mode (Ctrl+K); and with the cheats setting, 16 cheats (C, Shift+C,
keypad - and +, R, K, Shift+I, Shift+W, H, J, U, N, Ctrl+B, Shift+B, Shift+S,
Shift+T). Left out: Ctrl+Q (it ends the program) and the game's own saved
games, Ctrl+G and Ctrl+L (user-decided 2026-09-29: no native saves, no
menus). There are no letter keys; the hall of fame's name is the
`player_name` setting ("Chimera", user-decided), which patch 0003 enters in
place of `input_str`'s wait for keys - the printable characters, as many as fit
the box, up to 24. A blank one is refused at Init. SDLPoP's own keys (F6/F9, Tab, Backspace, the backtick, F12,
Ctrl+C) are on no button.

A modifier is a real key shared by every button that holds it: a Ctrl or Shift
command puts it down before its key and lifts it after, unless another held
button still holds it. The cheats' buttons are inactive without the setting
(`IsButtonActive`), so the frontend shows 17 columns or 33 and the driver
ignores an inactive one.

Two seams the commands needed: Shift+L starts an SDL timer (250 ms, giving the
Shift keys back if still held) - SDL's timers run on a thread, so the driver
runs them on the game's clock when a step reaching their time resumes, and
`SDL_GetKeyboardState` answers with the keys the buttons hold (before, the
wrapped `SDL_AddTimer` returned 0 and SDLPoP quit on Shift+L). And the files
the game writes - PRINCE.SAV (unreachable now), PRINCE.HOF, QUICKSAVE.SAV - live in
guest memory: `getenv("SDLPOP_SAVE_PATH")` is "saves", and fopen of
"saves/NAME" is an in-memory file, so a savestate carries them.

## Slot: savestate (2026-09-29)

(A `savegame` slot - a PRINCE.SAV for Load Game - was added and taken out the
same day, with the game's own saved games.)

- `savestate`: an SDLPoP quicksave (QUICKSAVE.SAV, "V1.16b4 " and then
  `quick_process`'s variables; its size is checked against this build's).
  The run starts from it on its first tick: Init sets `need_quick_load`, as F9
  on the title does, and names the first level to start so the title is not
  shown. The gate makes one with run-native's `--quicksave STEP:PATH`, which
  calls SDLPoP's `quick_save()` between two steps.



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

## The patches

`patches/0003` (2026-09-29), under `#ifdef CHIMERA_CORE`: the hall of fame's
name is `chimera_hof_name()`, entered as `input_str` would have taken it.

`patches/0002` (2026-09-29), under `#ifdef CHIMERA_CORE`: the copy
protection's four tables writable (`COPYPROT_TABLE`), the on-screen question
in "PAGE LINE WORD" order when `chimera_copyprot_page_first()`, and Ctrl+V
showing `chimera_version_text()`.

`patches/0001` adds, under `#ifdef CHIMERA_CORE`: the include of
`chimera-hooks.h` in `common.h`; `chimera_wait_for_timer` in
`do_simple_wait`; `chimera_input_read` in `read_key`, `read_keyb_control` and
`read_joyst_control`; `chimera_idle_step` in `showmessage`, `input_str` and
`play_level`'s wait for a sound. Upstream's own build is unchanged.

## The gate (waterbox/run-gate.sh)

99 legs, all green on 2026-09-29 with the user's four releases (57 on
2026-09-28, on what were in fact 1.4's files). The 2026-09-29 additions, each
seen to fail on a break of its own:

| Break | Leg that failed |
| --- | --- |
| 1.1 given 1.0's guard tables | releases:tables |
| 1.3 without level colours | releases:level-colours |
| 1.1 asking WORD first | releases:copy-protection |
| 1.3's Ctrl+V line "V1.4" | releases:version-line |
| no quickload of the savestate | slot:savestate |
| the hall of fame name hook returning "" | settings:player-name |
| SDL_AddTimer returning 0 (Shift+L quits) | commands:effects, cheats:effects, releases:copy-protection |
| no cheat word | cheats:effects, cheats:pictures |
| every button active | cheats:off |
| the driver taking inactive buttons | cheats:off (the cheats pressed on the title start a game) |
| a route-relevant fix on by default | settings:original-defaults, settings:master-switch-alone |
| the guard tables read from SDLPoP's unused defaults | releases:tables, releases:tables-played |
| no other-release detection | refuse:other-release |
| a wrong 1.3/1.4 hash | releases:boot and the legs that need 1.3/1.4 |

The frontend gate's `gui:cheats-project` (a 33-column project pressing More
Time on row 50) failed on a core that never activates the cheats; its first
form, with no cheat pressed, did not - Chimera drops a log column the core
does not have, silently.

The 2026-09-28 legs:

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
- the route puts the prince where it should (room, Kid.X, Kid.Y at steps
  100, 200 and 273, read through the property table), leaves level 1 on the
  step it should, goes on to level 2, and with copy protection to the
  potions level;
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
- SDLPoP's pause menu and replays are out of reach (their keys are on no
  button; the menu would change settings). Quicksaves only as the savestate
  slot.
- Linux only; the Windows build and CI (`chimera.yml`) are not written.
- `skip_title`'s first-key restart (above) is SDLPoP's; left as it is.
