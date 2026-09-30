#!/usr/bin/env python3
"""Writes a .chimeraProject for run-frontend.sh, by hand, as the wizard would.

A Prince of Persia project is its settings, its firmware pins (every data
file whose requiredWhen the settings and slots meet, pinned by SHA-1 - the
release's files, by the version setting) and an input log in the panel's own
button names (the cheats' only with the cheats setting on) - and, optionally,
a level set in the "levels" slot. The movie, if given, is one of the gate's (JaffarPlus's
|r|LRUDS| rows, # comments), turned into Chimera's log rows.

usage: make-project.py <package> <out.chimeraProject> <frames> [--movie <route>]
                       [--settings <json>] [--file <slot>=<path>]... [--log-out <path>]
                       [--press <frame>:<button name>]...
--log-out also writes the rows alone, as chimera-run takes a movie.
"""
import argparse
import hashlib
import json
import os
import zipfile


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def press(rows, spec, buttons):
    """--press FRAME:BUTTON NAME: that button held on that row too."""
    frame, name = spec.split(":", 1)
    rows[int(frame)][buttons.index(name)] = "x"


def rows_from(movie, frames, buttons):
    """The log rows over the buttons the core has active (a list of characters each, in the log's order): the route's
    |r|LRUDS| letters put under their buttons' names."""
    at = {name: i for i, name in enumerate(buttons)}
    rows = []
    if movie:
        for line in open(movie):
            if line.startswith("#") or not line.strip():
                continue
            r = line.strip()
            row = ["."] * len(buttons)
            for name, held, ch in (("P1 Up", r[5] == "U", "U"), ("P1 Down", r[6] == "D", "D"), ("P1 Left", r[3] == "L", "L"),
                                   ("P1 Right", r[4] == "R", "R"), ("P1 Shift", r[7] == "S", "S"), ("Restart Level", r[1] == "r", "A")):
                if held:
                    row[at[name]] = ch
            rows.append(row)
    while len(rows) < frames:
        rows.append(["."] * len(buttons))
    return rows[:frames]


def render(row, groups):
    """A log row: each group's characters, between separators (|commands|P1|)."""
    out, i = "|", 0
    for g in groups:
        out += "".join(row[i:i + len(g)]) + "|"
        i += len(g)
    return out


def holds(cond, settings, slots):
    if cond is None:
        return True
    if "not" in cond:
        return not holds(cond["not"], settings, slots)
    if "all" in cond:
        return all(holds(c, settings, slots) for c in cond["all"])
    if "any" in cond:
        return any(holds(c, settings, slots) for c in cond["any"])
    if "slot" in cond:
        return cond["slot"] in slots
    if "setting" in cond:
        v = settings.get(cond["setting"])
        return v in cond["in"] if "in" in cond else v == cond.get("is")
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("package")
    ap.add_argument("out")
    ap.add_argument("frames", type=int)
    ap.add_argument("--movie")
    ap.add_argument("--settings", default="{}")
    ap.add_argument("--file", action="append", default=[])
    ap.add_argument("--log-out")
    ap.add_argument("--press", action="append", default=[])
    a = ap.parse_args()

    cfg = json.loads(zipfile.ZipFile(a.package).read("waterbox.config"))
    settings = {d["name"]: d["default"] for d in cfg["settings"]}
    settings.update(json.loads(a.settings))
    # the cheats' buttons are active only with the cheats setting on
    # (IsButtonActive), and the log has only the active ones
    active = [b for b in cfg["input"]["buttons"] if settings.get("cheats") or not b.startswith("Cheat ")]
    # the log's groups, as Chimera orders them: the buttons of no player (the commands, the cheats), then P1's
    groups = [[b for b in active if not b.startswith("P1 ")], [b for b in active if b.startswith("P1 ")]]
    buttons = groups[0] + groups[1]
    rows = rows_from(a.movie, a.frames, buttons)
    for spec in a.press:
        press(rows, spec, buttons)
    rows = [render(r, groups) for r in rows]
    if a.log_out:
        open(a.log_out, "w").write("\n".join(rows) + "\n")
    log = "[Input]\nLogKey:" + "".join("#" + "".join(b + "|" for b in g) for g in groups) + "\n" + "\n".join(rows) + "\n[/Input]\n"

    files = []
    for spec in a.file:
        slot, path = spec.split("=", 1)
        files.append({"name": os.path.basename(path), "sha1": sha1(path), "slot": slot})
    slots = {f["slot"] for f in files}
    firmware = [{"id": d["id"], "sha1": d["sha1"]} for d in cfg["firmware"]
                if holds(d.get("requiredWhen"), settings, slots)]

    project = {
        "id": "sdlpop-frontend-gate",
        "title": "Prince of Persia through Chimera",
        "description": "written by waterbox/tests/run-frontend.sh",
        "core": {"name": cfg["coreName"], "version": cfg["version"], "sha1": sha1(a.package)},
        "rerecords": 0,
        "files": files,
        "settings": settings,
        "firmware": firmware,
        "coreCache": [],
        "input": log,
        "markers": [],
        "branches": [],
        "headers": {
            "MovieVersion": "Chimera Project File v1.1",
            "Platform": cfg["systemId"],
            "SHA1": files[0]["sha1"] if files else "",
            "LastInputFrame": str(a.frames - 1),
            "VsyncNumerator": str(cfg["video"]["vsyncNumerator"]),
            "VsyncDenominator": str(cfg["video"]["vsyncDenominator"]),
        },
    }
    with open(a.out, "w") as f:
        json.dump(project, f, indent="\t")


if __name__ == "__main__":
    main()
