#!/usr/bin/env python3
"""Writes waterbox.config: what the frontend is told about this core.

The settings that are SDLPoP's own options come from settings.inc, the same
table the driver builds the game's SDLPoP.ini from; the data files and their
hashes from the same list the driver checks at Init (sdlpop-driver.c's
k_files). tests/check-wire.py holds the three to each other.

usage: gen-config.py [<out>]   (default: waterbox/waterbox.config)
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def ini_settings():
    text = open(os.path.join(HERE, "settings.inc")).read()
    out = []
    for m in re.finditer(r'POP_SETTING\((\w+), (BOOL|INT), "(\w+)", (-?\d+), (-?\d+), (-?\d+), "([^"]*)",\s*"([^"]*)"\)', text):
        name, kind, _section, dflt, lo, hi, display, desc = m.groups()
        s = {"name": name, "display": display}
        if kind == "BOOL":
            s.update({"type": "bool", "default": dflt == "1"})
        else:
            s.update({"type": "int", "default": int(dflt), "min": int(lo), "max": int(hi)})
        s["description"] = desc
        out.append(s)
    return out


RELEASES = [("V10", "1.0"), ("V11", "1.1"), ("V13", "1.3"), ("V14", "1.4")]


def data_files():
    text = open(os.path.join(HERE, "sdlpop-driver.c")).read()
    out = []
    for n, mask, size, sha1, need in re.findall(
            r'\{ "([A-Z0-9_]+\.(?:DAT|EXE))", ((?:V1[0-4]\|?)+), (\d+), "([0-9A-F]{40})", (NEED_\w+) \}', text):
        vs = [name for sym, name in RELEASES if sym in mask.split("|")]
        out.append((n, vs, int(size), sha1, need))
    return out


def buttons():
    """The input.buttons names, in the driver's enum order (sdlpop-driver.h)."""
    text = open(os.path.join(HERE, "sdlpop-driver.h")).read()
    enum = text[text.index("enum PopButton"):text.index("POP_BTN_COUNT")]
    syms = [s for s in re.findall(r"^\s*(POP_BTN_\w+)(?:\s*=[^,]*)?,", enum, re.M) if s != "POP_BTN_CHEAT_FIRST"]
    return [BUTTON_NAMES[s] for s in syms]


BUTTON_NAMES = {
    # the prince's keys are P1's: Chimera logs them after the separator, the commands and the cheats before it
    # (|commands|inputs|)
    "POP_BTN_UP": "P1 Up", "POP_BTN_DOWN": "P1 Down", "POP_BTN_LEFT": "P1 Left", "POP_BTN_RIGHT": "P1 Right",
    "POP_BTN_SHIFT": "P1 Shift", "POP_BTN_ENTER": "P1 Enter",
    "POP_BTN_PAUSE": "Pause", "POP_BTN_SHOW_TIME": "Show Time", "POP_BTN_RESTART_LEVEL": "Restart Level",
    "POP_BTN_RESTART_GAME": "Restart Game", "POP_BTN_NEXT_LEVEL": "Next Level", "POP_BTN_SOUND_ON_OFF": "Sound On/Off", "POP_BTN_VERSION": "Version",
    "POP_BTN_CHEAT_SHOW_ROOMS": "Cheat Show Rooms", "POP_BTN_CHEAT_SHOW_CORNER_ROOMS": "Cheat Show Corner Rooms",
    "POP_BTN_CHEAT_LESS_TIME": "Cheat Less Time", "POP_BTN_CHEAT_MORE_TIME": "Cheat More Time",
    "POP_BTN_CHEAT_REVIVE": "Cheat Revive", "POP_BTN_CHEAT_KILL_GUARD": "Cheat Kill Guard",
    "POP_BTN_CHEAT_FLIP_SCREEN": "Cheat Flip Screen", "POP_BTN_CHEAT_FEATHER_FALL": "Cheat Feather Fall",
    "POP_BTN_CHEAT_LOOK_LEFT": "Cheat Look Left", "POP_BTN_CHEAT_LOOK_RIGHT": "Cheat Look Right",
    "POP_BTN_CHEAT_LOOK_UP": "Cheat Look Up", "POP_BTN_CHEAT_LOOK_DOWN": "Cheat Look Down",
    "POP_BTN_CHEAT_LOOK_BACK": "Cheat Look Back", "POP_BTN_CHEAT_BLIND_MODE": "Cheat Blind Mode",
    "POP_BTN_CHEAT_ADD_HIT_POINT": "Cheat Add Hit Point", "POP_BTN_CHEAT_ADD_MAX_HIT_POINT": "Cheat Add Max Hit Point",
}


WHAT = {
    "PRINCE.DAT": "the pictures that all levels share (the sword, the flames, the potions) and the palettes. In 1.3 and 1.4 it also holds the level colours",
    "KID.DAT": "the prince",
    "LEVELS.DAT": "the fifteen levels, with the tiles of every room, the doors and the guards",
    "TITLE.DAT": "the title screens, the story pages and the hall of fame",
    "PV.DAT": "the princess and Jaffar in the cutscenes",
    "VDUNGEON.DAT": "the walls and floors of the dungeon levels",
    "VPALACE.DAT": "the walls and floors of the palace levels",
    "GUARD.DAT": "the guards",
    "GUARD1.DAT": "the colours of the palace guards",
    "GUARD2.DAT": "the colours of the dungeon guards",
    "FAT.DAT": "the fat guard",
    "SKEL.DAT": "the skeleton",
    "VIZIER.DAT": "Jaffar",
    "SHADOW.DAT": "the shadow",
    "IBM_SND1.DAT": "the PC speaker sounds. The game opens this file whichever sound card is chosen",
    "IBM_SND2.DAT": "the PC speaker music. The game opens this file whichever sound card is chosen",
    "DIGISND1.DAT": "the digitized sound effects for the Sound Blaster",
    "DIGISND2.DAT": "the digitized cutscene and level sounds for the Sound Blaster",
    "DIGISND3.DAT": "the digitized sound effects for the Sound Blaster",
    "MIDISND1.DAT": "the AdLib music",
    "MIDISND2.DAT": "the AdLib music of the title and cutscenes",
    "PRINCE.EXE": "the release's program. It is only checked and never run, because SDLPoP is the program. It is the file that tells 1.0 from 1.1 and 1.3 from 1.4, whose data files are the same",
}


# each release as the System box names it
RELEASE_LABELS = [
    ("1.0", "1.0 (1990)"),
    ("1.1", "1.1 (IBM PC)"),
    ("1.3", "1.3 (1992)"),
    ("1.4", "1.4 (Collection CD)"),
]


# ---- what the controls and the system are called ----
# The frontend keeps no table of these: a core says what its own are called.
# MNEMONICS is the letter each button writes into a movie's text and heads its
# input column with, by the button's name - whole, or without its player ("P2
# Up" is found under "Up"), so one line serves every pad. AXIS_HEADERS is the
# short header of each axis's column. (An entry is read by position: a letter
# may change and no movie made before it is harmed.)
MNEMONICS = {
    "Up": "U", "Down": "D", "Left": "L", "Right": "R", "Shift": "^", "Enter": "e", "Pause": "p",
    "Show Time": "t", "Restart Level": "a", "Restart Game": "r", "Next Level": "n",
    "Sound On/Off": "s", "Version": "v", "Cheat Show Rooms": "C", "Cheat Show Corner Rooms": "Q",
    "Cheat Less Time": "-", "Cheat More Time": "+", "Cheat Revive": "V", "Cheat Kill Guard": "K",
    "Cheat Flip Screen": "I", "Cheat Feather Fall": "W", "Cheat Look Left": "4",
    "Cheat Look Right": "6", "Cheat Look Up": "8", "Cheat Look Down": "2", "Cheat Look Back": "5",
    "Cheat Blind Mode": "B", "Cheat Add Hit Point": "H", "Cheat Add Max Hit Point": "M",
}
SYSTEM_NAMES = {
    "PrinceOfPersia": "Prince of Persia",
}


def _bare(name):
    """A control's name without its player: "P2 Up" -> "Up"."""
    head, _, rest = name.partition(" ")
    return rest if rest and head[:1] == "P" and head[1:].isdigit() else name


def mnemonics_for(buttons):
    """The "mnemonics" of an input declaration: a letter for every one of its
    buttons, and for nothing else. A button nobody gave a letter stops the
    build - the engine would give it its rule's guess, and two columns of one
    pad would share a letter with nobody having decided it."""
    out = {}
    for b in buttons:
        key = b if b in MNEMONICS else _bare(b)
        if key not in MNEMONICS:
            raise SystemExit("no mnemonic for the button %r (MNEMONICS in %s)" % (b, __file__))
        out[key] = MNEMONICS[key]
    return out


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "waterbox.config")
    firmware = []
    for name, vs, size, sha1, need in data_files():
        which = "/".join(vs)
        conds = [{"setting": "version", "in": vs}]
        decl = {
            "id": name,
            "display": "Prince of Persia %s %s" % (which, name),
            "description": "%s of the original Prince of Persia %s (DOS): %s. You have to supply it. The package contains none of the game's data. A modified file of your own can be used instead, and the project records exactly which file it was." % (name, which, WHAT[name]),
            "size": size,
            "sha1": sha1,
            "name": name,
        }
        if need == "NEED_DIGITAL":
            conds.append({"setting": "sound", "in": ["digital"]})
        if need == "NEED_ORIGINAL_LEVELS":
            conds.append({"not": {"slot": "levels"}})
            decl["description"] += " It is not needed when the project has a level set of its own."
        decl["requiredWhen"] = conds[0] if len(conds) == 1 else {"all": conds}
        firmware.append(decl)

    cfg = {
        "coreName": "SDLPoP",
        "kind": "game",
        "systemNames": SYSTEM_NAMES,
        # the releases are the package's machines, so the new-project wizard offers
        # them where it offers an emulator's systems (its System box, beside the
        # core) rather than among the settings; the version setting stays what
        # picks one, so a project that already names its release keeps it
        "machineSetting": "version",
        "machines": [
            {"id": "PrinceOfPersia", "label": "Prince of Persia %s" % label, "when": [v]}
            for v, label in RELEASE_LABELS
        ],
        "author": "David Nagy and the SDLPoP contributors, from Jordan Mechner's Prince of Persia; chimera port by Sergio Martin",
        "url": "https://github.com/ToolAssisted-run/chimera-core-sdlpop",
        "deterministic": True,
        "memoryLayoutMiB": [64, 8, 8, 8, 64],
        "_memoryLayoutMiB_note": "sbrk, sealed, invisible, plain, mmap. The game itself is small; the room is for the sounds SDLPoP converts to 44100 Hz stereo when it loads them, SDL's dummy window, and the game's own 4 MiB stack (mmap, MAP_STACK).",
        "video": {
            "_comment": "The DOS game's 320x200 VGA screen, shown on a 4:3 monitor.",
            "width": 320,
            "height": 200,
            "virtualWidth": 320,
            "virtualHeight": 240,
            "vsyncNumerator": 12,
            "vsyncDenominator": 1,
            "_vsync_note": "A frame is one step of the game (docs/game-cores.md): a tick of play is 1/12 s walking and 1/10 s fighting; the title, the cutscenes and the pauses poll the controls every 1/60 s; a room change adds a 1/10 s step with the screen dark. GetVsyncNumerator/Denominator report the step just run; 12/1 is the rate of play.",
            "getBgra": "GetVideoBgra",
        },
        "audio": {
            "_comment": "SDLPoP's mixer, pulled at 44100 Hz stereo for exactly the time each step covers: 3675 pairs for a tick of play, 735 for a 1/60 s step.",
            "rate": 44100,
            "samplesPerFrame": 44100,
            "channels": 2,
            "get": "GetAudio",
        },
        "lag": {"inputWasRead": "InputWasRead"},
        "input": {
            "name": "Prince of Persia",
            "_comment": "The DOS game's keyboard: the arrows, Shift (grab, pick up, strike) and Enter (restart after dying, as Shift does); the game's commands, each its own button (Pause is Esc, Show Time is Space, Restart Level is Ctrl+A, Restart Game Ctrl+R, Next Level Shift+L, Sound On/Off Ctrl+S, Version Ctrl+V); and the cheats the game has when started with its cheat word, active only with the cheats setting on (Show Rooms C, Show Corner Rooms Shift+C, Less Time and More Time keypad - and +, Revive R, Kill Guard K, Flip Screen Shift+I, Feather Fall Shift+W, Look Left/Right/Up/Down H/J/U/N, Look Back Ctrl+B, Blind Mode Shift+B, Add Hit Point Shift+S, Add Max Hit Point Shift+T). Left out: Ctrl+Q (it ends the program), the game's saved games (Ctrl+G and Ctrl+L), and Joystick Mode and Keyboard Mode (Ctrl+J, Ctrl+K), which mean nothing to a movie. The hall of fame's name is the player_name setting, entered by itself.",
            "buttons": buttons(),
            "mnemonics": mnemonics_for(buttons()),
        },
        "settings": [
            {
                "name": "version",
                "display": "Version",
                "type": "enum",
                "options": ["1.0", "1.1", "1.3", "1.4"],
                "default": "1.0",
                "description": "Which release of Prince of Persia is played. The "
                    "project supplies that release's files. 1.0 (1990) and "
                    "1.1 (the IBM PC release) have the same data files. 1.3 "
                    "(1992) and 1.4 (the Prince of Persia Collection CD) "
                    "also have the same data files as each other. Those "
                    "differ from the earlier ones in the level colours (the "
                    "dungeon of level 3 is green, not blue), two sounds and "
                    "the music. The guards fight by the tables of 1.0 in "
                    "1.0, and by the later tables in 1.1, 1.3 and 1.4. The "
                    "copy protection asks from each release's own manual "
                    "(one for 1.0, one for 1.1, and one for 1.3 and 1.4). "
                    "Each release's PRINCE.EXE is checked too, because it is"
                    " the file that tells the two releases of a pair apart.",
            },
            {
                "name": "cheats",
                "display": "Enable Cheats",
                "type": "bool",
                "default": False,
                "description": "Starts the game with its cheat word, as the DOS game "
                    "was started from the command line (megahit, or improved"
                    " for 1.3 and 1.4). The cheat buttons exist only when "
                    "this is on. It also lets Next Level (Shift+L) skip any "
                    "level without cutting the time to 15 minutes.",
            },
            {
                "name": "player_name",
                "display": "Player Name (Hall of Fame)",
                "type": "string",
                "default": "Chimera",
                "description": "The name that a won game enters in the hall of fame "
                    "when its time earns a place there. It is entered as the"
                    " player would have typed it. Only printable characters "
                    "are used, and only as many as fit in the box (24 at "
                    "most). The controls have no letter keys, so the game "
                    "enters the name by itself.",
            },
            {
                "name": "sound",
                "display": "Sound card",
                "type": "enum",
                "options": ["digital", "pcSpeaker"],
                "default": "digital",
                "description": "'digital' is the Sound Blaster's digitized sound "
                    "effects and the AdLib music, as SDLPoP plays them. "
                    "'pcSpeaker' is the PC speaker's sounds. The game waits "
                    "for some sounds to end (the music at the end of a "
                    "level, the title). So it is part of the machine, and a "
                    "movie needs the same value. 'digital' needs the DIGISND"
                    " and MIDISND files.",
            },
            {
                "name": "random_seed",
                "display": "Random seed",
                "type": "int",
                "default": 0,
                "min": 0,
                "max": 2147483647,
                "description": "The starting value (seed) of the game's random number "
                    "generator. The DOS game took it from the clock. A movie"
                    " records the number it ran with.",
            },
        ] + ini_settings(),
        "firmware": firmware,
    }
    with open(out, "w") as f:
        json.dump(cfg, f, indent=2)
        f.write("\n")


if __name__ == "__main__":
    main()
