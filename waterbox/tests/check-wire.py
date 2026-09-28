#!/usr/bin/env python3
"""The declarations, twice over: waterbox.config against the sources it is
generated from and the driver it describes.

- the buttons are the driver's PopButton enum, in order
- the config is what gen-config.py writes now from settings.inc and the
  driver's data-file table (so nobody edited one and not the other)

usage: check-wire.py <repo root>
"""
import json
import os
import re
import subprocess
import sys
import tempfile

root = sys.argv[1]
wb = os.path.join(root, "waterbox")
cfg = json.load(open(os.path.join(wb, "waterbox.config")))
hdr = open(os.path.join(wb, "sdlpop-driver.h")).read()

body = re.search(r"enum PopButton\s*\{(.*?)\};", hdr, re.S).group(1)
enum = [t.split("=")[0].strip() for t in re.split(r"[,\n]", body)]
enum = [t for t in enum if t and not t.startswith("//") and not t.endswith("_COUNT")]
buttons = cfg["input"]["buttons"]
if len(enum) != len(buttons):
    sys.exit(f"driver has {len(enum)} buttons, waterbox.config {len(buttons)}")
norm = lambda s: re.sub(r"[^A-Z0-9]", "", s.upper())
for i, (e, b) in enumerate(zip(enum, buttons)):
    if norm(e.replace("POP_BTN_", "")) != norm(b):
        sys.exit(f"button {i}: driver {e} vs config '{b}'")

with tempfile.TemporaryDirectory() as tmp:
    fresh = os.path.join(tmp, "waterbox.config")
    subprocess.run([sys.executable, os.path.join(wb, "gen-config.py"), fresh], check=True)
    if json.load(open(fresh)) != cfg:
        sys.exit("waterbox.config is not what gen-config.py writes from settings.inc and the driver: regenerate it")

print(f"{len(buttons)} buttons, {len(cfg['settings'])} settings and {len(cfg['firmware'])} data files agree")
