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


def data_files():
    text = open(os.path.join(HERE, "sdlpop-driver.c")).read()
    return [(n, int(size), sha1, need) for n, size, sha1, need in
            re.findall(r'\{ "([A-Z0-9_]+\.DAT)", (\d+), "([0-9A-F]{40})", (NEED_\w+) \}', text)]


WHAT = {
    "PRINCE.DAT": "the pictures every level shares (the sword, the flames, the potions), the palettes and the level color tables",
    "KID.DAT": "the prince",
    "LEVELS.DAT": "the fifteen levels: every room's tiles, the doors, the guards",
    "TITLE.DAT": "the title screens, the story pages and the hall of fame",
    "PV.DAT": "the princess and Jaffar in the cutscenes",
    "VDUNGEON.DAT": "the dungeon levels' walls and floors",
    "VPALACE.DAT": "the palace levels' walls and floors",
    "GUARD.DAT": "the guards",
    "GUARD1.DAT": "the palace guards' colors",
    "GUARD2.DAT": "the dungeon guards' colors",
    "FAT.DAT": "the fat guard",
    "SKEL.DAT": "the skeleton",
    "VIZIER.DAT": "Jaffar",
    "SHADOW.DAT": "the shadow",
    "IBM_SND1.DAT": "the PC speaker sounds (opened whatever the sound card)",
    "IBM_SND2.DAT": "the PC speaker music (opened whatever the sound card)",
    "DIGISND1.DAT": "the Sound Blaster's digitized sound effects",
    "DIGISND2.DAT": "the Sound Blaster's digitized cutscene and level sounds",
    "DIGISND3.DAT": "the Sound Blaster's digitized sound effects",
    "MIDISND1.DAT": "the AdLib music",
    "MIDISND2.DAT": "the AdLib music of the title and cutscenes",
}


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "waterbox.config")
    firmware = []
    for name, size, sha1, need in data_files():
        decl = {
            "id": name,
            "display": "Prince of Persia 1.0 " + name,
            "description": "%s of the original Prince of Persia 1.0 (DOS): %s. Yours to supply - the package carries none of the game's data." % (name, WHAT[name]),
            "size": size,
            "sha1": sha1,
            "name": name,
        }
        if need == "NEED_DIGITAL":
            decl["requiredWhen"] = {"setting": "sound", "in": ["digital"]}
        if need == "NEED_ORIGINAL_LEVELS":
            decl["requiredWhen"] = {"not": {"slot": "levels"}}
            decl["description"] += " Not needed when the project brings a level set of its own."
        firmware.append(decl)

    cfg = {
        "coreName": "SDLPoP",
        "kind": "game",
        "systemId": "PrinceOfPersia",
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
            "_comment": "The DOS game's keyboard: the arrows, Shift (grab, pick up, strike), Enter (restart after dying, as Shift does) and Ctrl+A (restart the level, which a run may use).",
            "buttons": ["Up", "Down", "Left", "Right", "Shift", "Enter", "Restart Level"],
        },
        "settings": [
            {
                "name": "sound",
                "display": "Sound card",
                "type": "enum",
                "options": ["digital", "pcSpeaker"],
                "default": "digital",
                "description": "digital: the Sound Blaster's digitized effects and the AdLib music, as SDLPoP plays them. pcSpeaker: the PC speaker's sounds. The game waits for some sounds to end (a level's closing music, the title), so the sound card is part of the machine; digital needs the DIGISND and MIDISND files.",
            },
            {
                "name": "random_seed",
                "display": "Random seed",
                "type": "int",
                "default": 0,
                "min": 0,
                "max": 2147483647,
                "description": "The seed of the game's random number generator at power-on (the DOS game took it from the clock). A movie records the number it ran with.",
            },
        ] + ini_settings(),
        "firmware": firmware,
    }
    with open(out, "w") as f:
        json.dump(cfg, f, indent=2)
        f.write("\n")


if __name__ == "__main__":
    main()
