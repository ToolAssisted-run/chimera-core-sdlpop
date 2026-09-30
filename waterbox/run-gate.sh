#!/bin/bash
# The core gate. The sandboxed core must play Prince of Persia exactly as the
# native reference does (the same driver, SDLPoP and SDL built for the host) -
# picture, sound, every step's length and lag, the clock and every memory
# domain - survive a savestate before every step and a new host in the middle
# of a run, and then:
#   - show the title and level 1 as they are (pictures compared, and written
#     to build/gate for a person to look at)
#   - finish a known route: JaffarPlus's level 1 route leaves the level on the
#     step it should
#   - step at the game's own rates (a tick of play is 1/12 s, the title polls
#     at 60 Hz, a room change adds a dark 1/10 s step) and report lag honestly
#   - export a property table that holds to docs/game-cores.md, read the
#     same through it natively and sandboxed, obey a poke and hold a freeze
#   - take its settings (a first level, the minutes, the hit points arrive),
#     with every fix and enhancement off unless asked for
#   - be each release it is told to be (1.0, 1.1, 1.3, 1.4): its files, its
#     level colours, its guards' tables, its copy protection and its Ctrl+V
#   - obey every command key and, with the cheats setting, every cheat - and
#     no cheat without it
#   - start from a savestate (an SDLPoP quicksave), and enter the player's
#     name in the hall of fame by itself
#   - refuse a missing data file, and take a file of the project's own (a
#     changed one, another release's, SDLPoP's own) in the original's place
#   - package deterministically
#
# The data is the user's Prince of Persia, never in the repository: the gate
# takes each release from tests/roms-local/pop10, pop11, pop13 and pop14 (or
# -d <dir holding those>). Without 1.0's only the build, the declarations and
# the refusal of a project with no data run.
#
# Usage: ./run-gate.sh [-q] [-m <miniBox dir>] [-d <dir with pop10 pop11 pop13 pop14>]
#   -q skips the build (uses what is built)
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
mb="${MINIBOX_DIR:-$HOME/chimera/extern/chimera-common-minibox}"
roms="${POP_DIR:-$root/tests/roms-local}"
quick=0
while getopts "qm:d:" opt; do
	case "$opt" in
		q) quick=1 ;;
		m) mb="$OPTARG" ;;
		d) roms="$OPTARG" ;;
		*) exit 2 ;;
	esac
done
mb="$(cd "$mb" 2>/dev/null && pwd)" || { echo "miniBox not found; pass -m or set MINIBOX_DIR" >&2; exit 1; }
data="$roms/pop10"

nat="$root/build/native"
wbx="$root/build/guest/core.wbx"
work="$root/build/gate"
rm -rf "$work"
mkdir -p "$work"

ok=0
failed=0
skipped=0
report() {
	printf "%-30s %-6s %s\n" "$1" "$2" "$3"
	case "$2" in PASS) ok=$((ok+1)) ;; SKIP) skipped=$((skipped+1)) ;; *) failed=$((failed+1)) ;; esac
}
printf "%-30s %-6s %s\n" "Check" "Result" "Detail"
printf "%-30s %-6s %s\n" "-----" "------" "------"

digests() { grep -E '^(frames|vsync|videoHash|audioHash|stepsHash|lagFrames|clock|domain\[)'; }
# what a turbo run can be held to: all but the whole-run picture hash, which a
# run that skipped half its conversions cannot match - the half it drew is
# compared instead
turboDigests() { grep -E '^(frames|vsync|tailVideoHash|audioHash|stepsHash|lagFrames|clock|domain\[)'; }
# a game that stops waiting would spin forever; no run here takes a minute
native() { timeout 120 "$nat/run-native" "$@"; }
boxed() { timeout 120 "$nat/run-wbx" "$wbx" "$@"; }
# the property at a step of a trace (column 4 on is --trace-props, in order)
at() { awk -v s="$2" -v c="$3" '$1 == s { print $(3 + c) }' "$1"; }

# ------------------------------------------------------------------ 1. build
if [ "$quick" -eq 0 ]; then
	if make -C "$here" -f native.mk MB="$mb" -j"$(nproc)" > "$work/native-make.log" 2>&1 &&
	   make -C "$here" -f guest.mk MB="$mb" -j"$(nproc)" > "$work/guest-make.log" 2>&1; then
		report "build" PASS "native reference, harnesses and core.wbx (check-wbx clean)"
	else
		report "build" FAIL "see build/gate/*-make.log"
	fi
fi
[ -x "$nat/run-native" ] && [ -x "$nat/run-wbx" ] && [ -f "$wbx" ] || { echo "nothing built to test" >&2; exit 1; }
# a gate over an old core.wbx proves nothing about the sources (with -q above
# all): every source and the patch series must be older than what is tested
stale="$(find "$here" -maxdepth 1 \( -name '*.c' -o -name '*.h' -o -name '*.inc' -o -name '*.mk' \) \
	! -name 'run-*.c' ! -name 'gate-harness.h' -newer "$wbx" | head -3; find "$root/patches" -name '*.patch' -newer "$wbx" | head -1)"
if [ -n "$stale" ]; then
	report "build:fresh" FAIL "core.wbx is older than $(echo $stale | tr '\n' ' ')"
fi

# ------------------------------------------------------------------ 2. the declarations
if python3 "$here/tests/check-wire.py" "$root" > "$work/wire.txt" 2>&1; then
	report "wire:config==driver" PASS "$(cat "$work/wire.txt")"
else
	report "wire:config==driver" FAIL "$(tail -1 "$work/wire.txt")"
fi

# a work dir: the data files the project would mount (the release's: 1.0's
# unless a third argument names another's folder), and a settings file
workdir() {
	local wd="$work/$1" src="${3:-$data}"
	mkdir -p "$wd"
	cp "$src"/*.DAT "$src/PRINCE.EXE" "$wd/" 2>/dev/null
	rm -f "$wd/CONFIG.DAT" "$wd/SETUP.DAT"
	printf '%s' "$2" > "$wd/settings"
	echo "$wd"
}
# a picture's pixels, and its colour: how much bluer than green its top half is
tgapixels() { tail -c +19 "$1" 2>/dev/null | sha1sum | cut -c1-16; }
bluer() {
	python3 - "$1" <<'PYEOF'
import sys
d = open(sys.argv[1], "rb").read()[18:]
w, h = 320, 200
b = g = 0
for i in range(0, w * h // 2 * 4, 4):
    b += d[i]; g += d[i + 1]
print(b - g)
PYEOF
}

# ------------------------------------------------------------------ 3. no data
wd="$work/nodata"; mkdir -p "$wd"; printf '{}' > "$wd/settings"
boxed "$wd" --frames 1 > "$work/nodata.txt" 2>/dev/null
if grep -q '^loadError=Prince of Persia 1.0 needs PRINCE.DAT, KID.DAT, .*MIDISND2.DAT, PRINCE.EXE - add them as the project.s firmware' "$work/nodata.txt"; then
	report "refuse:no-data" PASS "every file named: $(sed -n 's/^loadError=//p' "$work/nodata.txt" | cut -c1-60)..."
else
	report "refuse:no-data" FAIL "$(head -1 "$work/nodata.txt")"
fi

if [ ! -f "$data/PRINCE.DAT" ]; then
	report "game" SKIP "no Prince of Persia 1.0 data in $data"
	echo; echo "$ok ok, $failed failed, $skipped skipped"
	[ "$failed" -eq 0 ]; exit
fi

# ------------------------------------------------------------------ 4. refusals
wd="$(workdir refuse-missing '{}')"; rm "$wd/KID.DAT"
boxed "$wd" --frames 1 > "$work/r1.txt" 2>/dev/null
native "$wd" --frames 1 > "$work/r1n.txt" 2>/dev/null
if grep -qx 'loadError=Prince of Persia 1.0 needs KID.DAT - add it as the project.s firmware.' "$work/r1.txt" && cmp -s <(grep loadError "$work/r1.txt") <(grep loadError "$work/r1n.txt"); then
	report "refuse:missing-file" PASS "$(sed -n 's/^loadError=//p' "$work/r1.txt")"
else
	report "refuse:missing-file" FAIL "$(grep -m1 . "$work/r1.txt")"
fi

# a file of the project's own is taken in the original's place (Chimera pins
# its hash; user-decided 2026-09-29): a LEVELS.DAT with room 1's tile 10 made
# plain floor is the level the game has; SDLPoP's own DIGISND1.DAT, and - with
# the other releases' data at hand - 1.4's PRINCE.DAT and 1.1's PRINCE.EXE in
# a 1.0 project, are each taken
wd="$(workdir custom-orig '{"skip_title":true}')"
boxed "$wd" --frames 2 --trace "$work/custom-orig.trace" --trace-props "Room 1.Tile[10]" > "$work/custom-orig.txt" 2>/dev/null
wd="$(workdir custom-levels '{"skip_title":true}')"
python3 "$here/tests/make-levels.py" "$wd/LEVELS.DAT" "$wd/LEVELS.NEW" 1 1 10 1 && mv "$wd/LEVELS.NEW" "$wd/LEVELS.DAT"
boxed "$wd" --frames 2 --trace "$work/custom-levels.trace" --trace-props "Room 1.Tile[10]" > "$work/custom-levels.txt" 2>/dev/null
taken=""
wd="$(workdir custom-sdlpop-own '{}')"; cp "$root/extern/SDLPoP/data/DIGISND1.DAT" "$wd/DIGISND1.DAT"
boxed "$wd" --frames 30 2>/dev/null | grep -qx 'frames=30' && taken="$taken DIGISND1.DAT(SDLPoP's)"
if [ -f "$roms/pop14/PRINCE.DAT" ] && [ -f "$roms/pop11/PRINCE.EXE" ]; then
	wd="$(workdir custom-other '{}')"; cp "$roms/pop14/PRINCE.DAT" "$roms/pop11/PRINCE.EXE" "$wd/"
	boxed "$wd" --frames 30 2>/dev/null | grep -qx 'frames=30' && taken="$taken PRINCE.DAT(1.4)+PRINCE.EXE(1.1)"
	want=" DIGISND1.DAT(SDLPoP's) PRINCE.DAT(1.4)+PRINCE.EXE(1.1)"
else
	want=" DIGISND1.DAT(SDLPoP's)"
fi
if [ "$(at "$work/custom-levels.trace" init 1)" = "1" ] && [ "$(at "$work/custom-orig.trace" init 1)" != "1" ] &&
   ! grep -q loadError "$work/custom-levels.txt" && [ "$taken" = "$want" ]; then
	report "firmware:custom" PASS "a LEVELS.DAT of the project's own is the level played (room 1 tile 10: $(at "$work/custom-orig.trace" init 1) -> 1); taken:$taken"
else
	report "firmware:custom" FAIL "tile 10 $(at "$work/custom-orig.trace" init 1) -> $(at "$work/custom-levels.trace" init 1); taken [$taken] of [$want]; $(grep -m1 loadError "$work/custom-levels.txt")"
fi
wd="$(workdir refuse-version '{"version":"1.2"}')"
boxed "$wd" --frames 1 > "$work/r7.txt" 2>/dev/null
if grep -qx "loadError=unknown version setting '1.2' (1.0, 1.1, 1.3 or 1.4)" "$work/r7.txt"; then
	report "refuse:unknown-version" PASS "a version that is none of the four is named"
else
	report "refuse:unknown-version" FAIL "$(grep -m1 . "$work/r7.txt")"
fi

wd="$(workdir speaker-files '{"sound":"pcSpeaker"}')"; rm "$wd"/DIGISND*.DAT "$wd"/MIDISND*.DAT
if boxed "$wd" --frames 30 > "$work/r4.txt" 2>&1 && grep -q '^frames=30' "$work/r4.txt"; then
	report "firmware:speaker-needs-less" PASS "with the PC speaker the DIGISND and MIDISND files are not needed"
else
	report "firmware:speaker-needs-less" FAIL "$(grep -m1 . "$work/r4.txt")"
fi

# a level set of the project's own (the "levels" slot) is played in place of
# LEVELS.DAT, which is then not needed: one tile changed - room 1's tile 10,
# the torch floor level 1 starts on, made plain floor - is the tile the game has
wd="$(workdir levelset '{"skip_title":true}')"
python3 "$here/tests/make-levels.py" "$wd/LEVELS.DAT" "$wd/MYLEVELS.DAT" 1 1 10 1
rm "$wd/LEVELS.DAT"
printf '{"levels":["MYLEVELS.DAT"]}' > "$wd/slots"
boxed "$wd" --frames 2 --trace "$work/levelset.trace" --trace-props "Room 1.Tile[10],Room 1.Tile Flag[10],Room 1.Tile[11]" > "$work/levelset.txt" 2>/dev/null
if [ "$(at "$work/levelset.trace" init 1)" = "1" ] && [ "$(at "$work/levelset.trace" init 2)" = "1" ] && [ "$(at "$work/levelset.trace" init 3)" = "19" ]; then
	report "slot:levels" PASS "MYLEVELS.DAT played without the original LEVELS.DAT: room 1 tile 10 is floor (1), its flag kept, tile 11 a torch"
else
	report "slot:levels" FAIL "tile 10 $(at "$work/levelset.trace" init 1) flag $(at "$work/levelset.trace" init 2), tile 11 $(at "$work/levelset.trace" init 3): $(grep -m1 loadError "$work/levelset.txt")"
fi
rm "$wd/MYLEVELS.DAT"
boxed "$wd" --frames 1 > "$work/levelset2.txt" 2>/dev/null
if grep -qx "loadError=the project's level set MYLEVELS.DAT is not there" "$work/levelset2.txt"; then
	report "slot:levels-missing" PASS "a level set the slot names but the project did not mount is refused by name"
else
	report "slot:levels-missing" FAIL "$(grep -m1 . "$work/levelset2.txt")"
fi

# ------------------------------------------------------------------ 5. the runs
route="$here/tests/lvl01-route.txt"
props="Level.Current,Level.Next,Kid.Room,Kid.X,Kid.Y,Kid.HP,Time.Minutes Left,Time.Ticks Left"
# what the command and cheat runs read
cprops="Level.Current,Time.Minutes Left,Time.Ticks Left,Kid.HP,Kid.Max HP,Kid.Alive,Level.Drawn Room,Level.Upside Down,Effects.Feather Fall,Guard.Alive,Guard.HP,Kid.Room,Level.Next,Kid.X"
# every fix and enhancement on, one by one (they all default to off)
allfixes="$(python3 -c "
import json, re
s = {'skip_title': True, 'enable_copyprot': False, 'use_fixes_and_enhancements': True}
for n in re.findall(r'POP_SETTING\((\w+), BOOL, \"Enhancements\"', open('$here/settings.inc').read()):
    s[n] = True
print(json.dumps(s, separators=(',', ':')))")"
# SDLPoP's own quicksave of level 1, taken natively between two steps, for the
# "savestate" slot: the run below starts where this one stood at step 650
if [ -f "$data/PRINCE.DAT" ]; then
	wd="$(workdir makestate '{"enable_copyprot":false}')"
	native "$wd" --frames 651 --press 400:S:2 --press 600:R:30 --quicksave "650:$work/QUICKSAVE.SAV" \
		--trace "$work/makestate.trace" --trace-props "$props" > /dev/null 2>&1
fi
# name, steps, settings, release (pop10 unless said); the arguments are test_args's
tests=(
	"title|700|{}"
	"route|1100|{\"skip_title\":true,\"enable_copyprot\":false}"
	"copyprot|1150|{\"skip_title\":true}"
	"speaker|500|{\"sound\":\"pcSpeaker\"}"
	"fixes|420|$allfixes"
	"route14|300|{\"version\":\"1.4\",\"skip_title\":true,\"enable_copyprot\":false}|pop14"
	"commands|1300|{\"enable_copyprot\":false,\"first_level\":3}"
	"cheats|1400|{\"cheats\":true,\"enable_copyprot\":false}"
	"fromstate|200|{\"enable_copyprot\":false}"
)
test_args() {
	case "$1" in
		# the title sequence, then Shift starts a game (level 1, copy protection on)
		title) args=(--press 400:S:2 --screenshot "299:$work/title.tga" --screenshot "450:$work/level1.tga") ;;
		# JaffarPlus's level 1 route, from the first tick of level 1
		# on through the princess's cutscene and the wait for its music to end
		# (a loop that waits for a sound without a delay) into level 2
		route) args=(--movie "$route" --poke "0:Random Seed=0" --screenshot "560:$work/cutscene.tga" --screenshot "1099:$work/level2.tga") ;;
		fixes) args=(--movie "$route" --poke "0:Random Seed=0") ;;
		route14) args=(--movie "$route" --poke "0:Random Seed=0") ;;
		# the same route with copy protection: the potions level and its question
		copyprot) args=(--movie "$route" --poke "0:Random Seed=0" --press 1110:R:2 --screenshot "1100:$work/manual.tga") ;;
		speaker) args=(--press 400:S:2) ;;
		# the command keys, each once, from level 3: Restart Level after the
		# prince has run, Restart Game to the title, a new game, Next Level
		commands) args=(--press 400:S:2 --press 530:T:1 --press 560:v:1
			--press 650:o:1 --press 700:P:1 --press 750:T:1 --press 760:R:15 --press 780:A:1 --press 800:G:1 --press 1000:S:2 --press 1100:N:1
			--screenshot "535:$work/cmd-time.tga" --screenshot "565:$work/cmd-version.tga"
			--screenshot "655:$work/cmd-sound.tga"
			--screenshot "720:$work/cmd-paused.tga" --audio "$work/commands.pcm") ;;
		# every cheat, from level 1 (Revive once the prince is dead), and Kill
		# Guard at level 2's first guard, reached by Next Level
		cheats) args=(--press 400:S:2 --press 460:+:1 --press 470:-:1 --press 480:M:1 --poke "490:Kid.HP=2" --press 500:H:1
			--press 520:4:1 --press 530:6:1 --press 540:2:1 --press 550:8:1 --press 560:4:1 --press 570:5:1
			--press 590:I:1 --press 600:I:1 --poke "620:Kid.HP=0" --press 700:V:1 --press 720:W:1
			--press 760:B:1 --press 770:B:1 --press 780:c:1 --press 800:C:1 --press 850:N:1 --press 1252:L:30 --press 1300:K:1
			--screenshot "765:$work/cheat-blind.tga" --screenshot "785:$work/cheat-rooms.tga" --screenshot "805:$work/cheat-corners.tga") ;;
		# from SDLPoP's quicksave, and on
		fromstate) args=(--press 100:R:20) ;;
	esac
}
for t in "${tests[@]}"; do
	IFS='|' read -r name frames settings release <<< "$t"
	if [ -n "$release" ] && [ ! -f "$roms/$release/PRINCE.DAT" ]; then
		report "$name" SKIP "no $release data in $roms"; continue
	fi
	wd="$(workdir "$name" "$settings" "$roms/${release:-pop10}")"
	if [ "$name" = fromstate ]; then
		cp "$work/QUICKSAVE.SAV" "$wd/MY.SAV" 2>/dev/null
		printf '{"savestate":["MY.SAV"]}' > "$wd/slots"
	fi
	test_args "$name"
	args+=(--frames "$frames")
	# the trace the legs below read is the first sandboxed run's; the native
	# run keeps its own, which has to be the same
	case "$name" in commands|cheats) trace=(--trace-props "$cprops" --trace) ;; *) trace=(--trace-props "$props" --trace) ;; esac

	if ! native "$wd" "${args[@]}" --props-json "$work/$name.native.json" "${trace[@]}" "$work/$name.native.trace" > "$work/$name.native.txt" 2> "$work/$name.native.err"; then
		report "$name:equivalence" FAIL "native runner: $(tail -1 "$work/$name.native.err")"; continue
	fi
	if ! boxed "$wd" "${args[@]}" --props-json "$work/$name.box.json" "${trace[@]}" "$work/$name.trace" > "$work/$name.box.txt" 2> "$work/$name.box.err"; then
		report "$name:equivalence" FAIL "sandbox runner: $(tail -1 "$work/$name.box.err")"; continue
	fi
	digests < "$work/$name.native.txt" > "$work/nat.txt"
	digests < "$work/$name.box.txt" > "$work/box.txt"
	# the sandbox against itself first: a second process must agree whatever
	# the native reference says
	boxed "$wd" "${args[@]}" 2>/dev/null | digests > "$work/again.txt"
	if cmp -s "$work/box.txt" "$work/again.txt"; then
		report "$name:determinism" PASS "a second sandboxed run is the same"
	else
		report "$name:determinism" FAIL "$(diff "$work/box.txt" "$work/again.txt" | tr '\n' ' ' | head -c 110)"
	fi
	if cmp -s "$work/nat.txt" "$work/box.txt" && cmp -s "$work/$name.native.trace" "$work/$name.trace"; then
		report "$name:equivalence" PASS "$frames steps, native == sandboxed ($(grep -c . "$work/box.txt") digests and every step's properties)"
	else
		report "$name:equivalence" FAIL "$(diff "$work/nat.txt" "$work/box.txt" | tr '\n' ' ' | head -c 110)$(cmp -s "$work/$name.native.trace" "$work/$name.trace" || echo ' (property traces differ)')"; continue
	fi

	boxed "$wd" --frames "$frames" 2>/dev/null | digests > "$work/idle.txt"
	if cmp -s "$work/box.txt" "$work/idle.txt"; then
		report "$name:input-shaped" FAIL "the input changed nothing"
	else
		report "$name:input-shaped" PASS "the same steps with no input are another game"
	fi

	boxed "$wd" "${args[@]}" 2>/dev/null | turboDigests > "$work/tnorm.txt"
	boxed "$wd" "${args[@]}" --turbo 2>/dev/null | turboDigests > "$work/turbo.txt"
	if cmp -s "$work/tnorm.txt" "$work/turbo.txt"; then
		report "$name:turbo" PASS "half the pictures unconverted, same game and same second half"
	else
		report "$name:turbo" FAIL "$(diff "$work/tnorm.txt" "$work/turbo.txt" | tr '\n' ' ' | head -c 110)"
	fi

	boxed "$wd" "${args[@]}" --rerecord 2>/dev/null | digests > "$work/rr.txt"
	if cmp -s "$work/box.txt" "$work/rr.txt"; then
		report "$name:savestate" PASS "saved and loaded before every step: lossless"
	else
		report "$name:savestate" FAIL "$(diff "$work/box.txt" "$work/rr.txt" | tr '\n' ' ' | head -c 110)"
	fi

	boxed "$wd" "${args[@]}" --session 2> "$work/ss.err" | digests > "$work/ss.txt"
	if cmp -s "$work/box.txt" "$work/ss.txt"; then
		report "$name:session" PASS "a new host finished the run from a state at step $((frames / 2))"
	else
		report "$name:session" FAIL "$(diff "$work/box.txt" "$work/ss.txt" | tr '\n' ' ' | head -c 110)"
	fi
done

# ------------------------------------------------------------------ 6. what the runs showed
# the pictures: the title's "presents" card and level 1's first room, as the
# gate last saw them (look at build/gate/*.png)
png() { python3 "$here/tests/tga2png.py" "$work/$1.tga" "$work/$1.png" 2 2>/dev/null; }
pixels() { tail -c +19 "$work/$1.tga" | sha1sum | cut -c1-16; }
for shot in "title:299 (Broderbund presents):635e453723de3bc3" "level1:450 (level 1, room 1):736ecb4c481d715d" \
	"cutscene:560 (the princess waits):c6020673a280e76c" "level2:1099 (level 2's first room):27bfbc1167e84d3e" \
	"manual:1100 (the potions level's manual question):a4181980a53adbbd"; do
	IFS=':' read -r file what want <<< "$shot"
	png "$file"
	got="$(pixels "$file")"
	if [ "$got" = "$want" ]; then
		report "picture:$file" PASS "step $what is the picture it was (build/gate/$file.png)"
	else
		report "picture:$file" FAIL "step $what: pixels $got, expected $want (build/gate/$file.png)"
	fi
done

# the route: JaffarPlus's level 1 route (see tests/lvl01-route.txt) takes the
# prince through the exit door on its last row, step 272, in room 9; the game
# moves on to level 2 in the step after - not a step earlier
tr="$work/route.trace"
# where the route has him on the way: (156,181) in room 1 at step 100, (80,118)
# in room 9 at step 200, and in the exit door at (134,90) at the end
pos() { echo "$(at "$tr" "$1" 3):$(at "$tr" "$1" 4),$(at "$tr" "$1" 5)"; }
if [ "$(at "$tr" 272 2)" = "1" ] && [ "$(at "$tr" 273 2)" = "2" ] && [ "$(at "$tr" 273 6)" = "3" ] &&
   [ "$(pos 100)" = "1:156,181" ] && [ "$(pos 200)" = "9:80,118" ] && [ "$(pos 273)" = "9:134,90" ]; then
	report "route:level-1" PASS "Kid at 1:156,181 (step 100), 9:80,118 (200), in the exit door 9:134,90 with 3 HP; Level.Next 2 from step 273"
else
	report "route:level-1" FAIL "next level $(at "$tr" 272 2)/$(at "$tr" 273 2) at 272/273, HP $(at "$tr" 273 6); room:x,y $(pos 100) $(pos 200) $(pos 273)"
fi
# and on to level 2 through the princess's cutscene (copy protection off) or
# to the potions level (on, the default); that one's manual question wants a key
if [ "$(at "$tr" 1099 1)" = "2" ] && [ "$(at "$tr" 1099 3)" = "5" ]; then
	report "route:level-2" PASS "through the cutscene and its music into level 2, where the prince starts in room 5, by step 1099"
else
	report "route:level-2" FAIL "level $(at "$tr" 1099 1), room $(at "$tr" 1099 3) at step 1099"
fi
cp_level="$(at "$work/copyprot.trace" 1149 1)"
if [ "$cp_level" = "15" ]; then
	report "route:copy-protection" PASS "with copy protection level 1 leads to the potions level (15), and a key clears its question"
else
	report "route:copy-protection" FAIL "level $cp_level at step 1149"
fi

# the rates: a tick of play is 1/12 s, a room change adds a dark 1/10 s step
# (and the tick after it is short by as much), the title's fades step at 30 Hz
# and its cards poll at 60 Hz; play reads the controls every step
rate() { awk -v s="$2" '$1 == s { print $2 }' "$1"; }
rc="$(awk '$2 == "10/1" { print $1; exit }' "$tr")"
if [ "$(rate "$tr" 50)" = "12/1" ] && [ -n "$rc" ] && [ "$(rate "$tr" $((rc + 1)))" = "15/1" ] &&
   [ "$(rate "$work/title.trace" 10)" = "30/1" ] && [ "$(rate "$work/title.trace" 100)" = "60/1" ] && [ "$(rate "$work/title.trace" 450)" = "12/1" ]; then
	report "steps:rates" PASS "play 12/1, room change 10/1 then 15/1 (step $rc), title fades 30/1, title cards 60/1"
else
	report "steps:rates" FAIL "play $(rate "$tr" 50), room change at ${rc:-none}, title $(rate "$work/title.trace" 10) $(rate "$work/title.trace" 100)"
fi
lag_play="$(awk '$1 ~ /^[0-9]+$/ && $1 <= 272 && $3 == 0' "$tr" | wc -l)"
lag_title="$(awk '$1 ~ /^[0-9]+$/ && $1 < 400 && $3 == 0 { printf "%s ", $1 }' "$work/title.trace")"
if [ "$lag_play" = "0" ] && [ "$lag_title" = "63 " ]; then
	report "steps:lag" PASS "every step of play reads the controls; the title's one lag step is 63 (a fade ending)"
else
	report "steps:lag" FAIL "$lag_play lag steps in play; title lag steps: $lag_title"
fi

# the property table: the format, and the same table natively and sandboxed
if python3 "$here/tests/check-properties.py" "$work/route.box.json" "$work/route.box.txt" > "$work/props.txt" 2>&1 &&
   cmp -s "$work/route.box.json" "$work/route.native.json"; then
	report "properties:table" PASS "$(cat "$work/props.txt")"
else
	report "properties:table" FAIL "$(tail -1 "$work/props.txt")"
fi

# a poke is what the game finds: the prince moved while he crouches for the
# opening music, and Level.Next set to 3, which the game then goes to
wd="$work/route"
boxed "$wd" --frames 700 --poke "20:Kid.X=120" --poke "60:Level.Next=3" --trace "$work/poke.trace" --trace-props "$props" > /dev/null 2>&1
native "$wd" --frames 700 --poke "20:Kid.X=120" --poke "60:Level.Next=3" --trace "$work/poke.native.trace" --trace-props "$props" > /dev/null 2>&1
if [ "$(at "$work/poke.trace" 19 4)" != "120" ] && [ "$(at "$work/poke.trace" 20 4)" = "120" ] && [ "$(at "$work/poke.trace" 30 4)" = "120" ] &&
   [ "$(at "$work/poke.trace" 699 1)" = "3" ] && cmp -s "$work/poke.trace" "$work/poke.native.trace"; then
	report "properties:poke" PASS "Kid.X=120 before step 20 holds him there; Level.Next=3 before step 60 takes the game to level 3"
else
	report "properties:poke" FAIL "Kid.X $(at "$work/poke.trace" 19 4)->$(at "$work/poke.trace" 20 4), level at 699: $(at "$work/poke.trace" 699 1)"
fi

# the level is the game's own memory: a bit field poked in place is obeyed -
# the floor under the prince (room 1, tile 10) made empty, and he falls through
# to the room below
boxed "$wd" --frames 100 --poke "70:Room 1.Tile[10]=0" --trace "$work/floor.trace" --trace-props "Kid.Room,Kid.Y,Room 1.Tile[10],Room 1.Tile Flag[10]" > /dev/null 2>&1
if [ "$(at "$work/floor.trace" 69 3)" = "19" ] && [ "$(at "$work/floor.trace" 70 3)" = "0" ] && [ "$(at "$work/floor.trace" 70 4)" = "1" ] &&
   [ "$(at "$work/floor.trace" 69 1)" = "1" ] && [ "$(at "$work/floor.trace" 99 1)" != "1" ]; then
	report "properties:poke-in-place" PASS "Room 1.Tile[10]=0 (bits 0-4, the flag bit kept): the torch floor he stands on goes and he falls to room $(at "$work/floor.trace" 99 1)"
else
	report "properties:poke-in-place" FAIL "tile $(at "$work/floor.trace" 69 3)->$(at "$work/floor.trace" 70 3) flag $(at "$work/floor.trace" 70 4), room $(at "$work/floor.trace" 69 1)->$(at "$work/floor.trace" 99 1)"
fi

# a freeze is the same write before every step: the clock's ticks held at 700
# (the step then counts one down, so 699 is what a step leaves) keep the minute
# from ever running out; without it, level 1 is at 59 minutes by step 799
boxed "$wd" --frames 800 --freeze "0-799:Time.Ticks Left=700" --trace "$work/freeze.trace" --trace-props "$props" > /dev/null 2>&1
boxed "$wd" --frames 800 --trace "$work/nofreeze.trace" --trace-props "$props" > /dev/null 2>&1
if [ "$(at "$work/freeze.trace" 799 7)" = "60" ] && [ "$(at "$work/freeze.trace" 799 8)" = "699" ] && [ "$(at "$work/nofreeze.trace" 799 7)" = "59" ]; then
	report "properties:freeze" PASS "Time.Ticks Left held at 700 for 800 steps: 60 minutes left (59 without the freeze)"
else
	report "properties:freeze" FAIL "frozen: $(at "$work/freeze.trace" 799 7) min $(at "$work/freeze.trace" 799 8) ticks; free: $(at "$work/nofreeze.trace" 799 7)"
fi

# a tick is 1/10 s while the prince's sword is drawn: held drawn by a freeze
boxed "$wd" --frames 320 --freeze "300-310:Kid.Sword=2" --trace "$work/sword.trace" --trace-props "Kid.Sword" > /dev/null 2>&1
fight="$(awk '$1 ~ /^[0-9]+$/ && $1 >= 300 && $1 <= 310 { print $2 }' "$work/sword.trace" | sort -u | tr '\n' ' ')"
if [ "$(rate "$work/sword.trace" 299)" = "12/1" ] && [ "$fight" = "10/1 " ]; then
	report "steps:fight-rate" PASS "with the sword drawn (Kid.Sword frozen at 2) every tick is 10/1"
else
	report "steps:fight-rate" FAIL "before: $(rate "$work/sword.trace" 299), drawn: $fight"
fi

# the settings reach the game: start on level 3 with 5 minutes and 5 hit points
wd="$(workdir settings '{"skip_title":true,"first_level":3,"start_minutes_left":5,"start_hitp":5}')"
boxed "$wd" --frames 2 --trace "$work/settings.trace" --trace-props "$props" > /dev/null 2>&1
if [ "$(at "$work/settings.trace" init 1)" = "3" ] && [ "$(at "$work/settings.trace" init 7)" = "5" ] && [ "$(at "$work/settings.trace" init 6)" = "5" ]; then
	report "settings:reach-the-game" PASS "first_level 3, start_minutes_left 5, start_hitp 5: level 3, 5 minutes, 5 hit points"
else
	report "settings:reach-the-game" FAIL "level $(at "$work/settings.trace" init 1), minutes $(at "$work/settings.trace" init 7), HP $(at "$work/settings.trace" init 6)"
fi
if ! cmp -s <(grep audioHash "$work/speaker.box.txt") <(boxed "$work/title" --frames 500 --press 400:S:2 2>/dev/null | grep audioHash); then
	report "settings:sound-card" PASS "the PC speaker sounds unlike the Sound Blaster"
else
	report "settings:sound-card" FAIL "the same sound either way"
fi

# ------------------------------------------------------------------ 6b. nothing of SDLPoP's by default
if python3 - "$here/waterbox.config" > "$work/defaults.txt" 2>&1 <<'PYEOF'
import json, sys
cfg = json.load(open(sys.argv[1]))
on = [s["name"] for s in cfg["settings"] if s["type"] == "bool" and s["default"] and s["name"] != "enable_copyprot"]
fixes = [s["name"] for s in cfg["settings"] if s["name"].startswith(("fix_", "enable_")) and s["name"] != "enable_copyprot"]
version = [s for s in cfg["settings"] if s["name"] == "version"][0]["default"]
if on or version != "1.0":
    sys.exit("on by default: %s; version %s" % (on, version))
print("%d fixes and enhancements, the cheats and the master switch default off; copy protection on; version 1.0" % len(fixes))
PYEOF
then
	report "settings:original-defaults" PASS "$(cat "$work/defaults.txt")"
else
	report "settings:original-defaults" FAIL "$(tail -1 "$work/defaults.txt")"
fi
# the master switch alone turns nothing on: the route plays as without it
# (and with every fix chosen too, it does not)
wd="$(workdir master '{"skip_title":true,"enable_copyprot":false,"use_fixes_and_enhancements":true}')"
boxed "$wd" --frames 420 --movie "$route" --poke "0:Random Seed=0" 2>/dev/null | digests > "$work/master.txt"
boxed "$work/route" --frames 420 --movie "$route" --poke "0:Random Seed=0" 2>/dev/null | digests > "$work/plain420.txt"
if [ -s "$work/master.txt" ] && cmp -s "$work/master.txt" "$work/plain420.txt" && ! cmp -s "$work/master.txt" <(digests < "$work/fixes.box.txt" 2>/dev/null); then
	report "settings:master-switch-alone" PASS "use_fixes_and_enhancements on, no fix chosen: the route is the original game's (every fix on: another game)"
else
	report "settings:master-switch-alone" FAIL "$(diff "$work/plain420.txt" "$work/master.txt" | head -2 | tr '\n' ' ')"
fi

# ------------------------------------------------------------------ 6c. the releases
# the bottom line of a picture (the game's messages), as pixels
strip() { tail -c +19 "$1" 2>/dev/null | head -c $((320 * 200 * 4)) | tail -c $((320 * 16 * 4)) | sha1sum | cut -c1-16; }
have_all=1
for r in pop11 pop13 pop14; do [ -f "$roms/$r/PRINCE.DAT" ] || have_all=0; done
if [ "$have_all" -eq 0 ]; then
	report "releases" SKIP "not every release's data in $roms (pop10 pop11 pop13 pop14)"
else
	for r in 10 11 13 14; do
		v="${r:0:1}.${r:1:1}"
		# the title, Ctrl+V on level 1, then Next Level straight to the potions
		# level (copy protection on) and its question
		wd="$(workdir "rel$r" "{\"version\":\"$v\"}" "$roms/pop$r")"
		boxed "$wd" --frames 481 --press 400:S:2 --press 450:v:1 --press 470:N:1 --screenshot "299:$work/rel$r-title.tga" \
			--screenshot "455:$work/rel$r-version.tga" --screenshot "480:$work/rel$r-question.tga" \
			--trace "$work/rel$r.trace" --trace-props "Guard Skills.Strike Probability[0],Guard Skills.Restrike Probability[6],Guard Skills.Impaired Block Probability[1],Guard Skills.Refractory Timer[0],Level Colours[3],Level Colours[14],Level.Current" \
			> "$work/rel$r.txt" 2>/dev/null
		# level 3, where 1.3 changed the dungeon's colour
		wd="$(workdir "rel$r-l3" "{\"version\":\"$v\",\"skip_title\":true,\"first_level\":3}" "$roms/pop$r")"
		boxed "$wd" --frames 40 --screenshot "39:$work/rel$r-level3.tga" > /dev/null 2>&1
	done
	boot=""; for r in 10 11 13 14; do grep -q '^frames=481' "$work/rel$r.txt" && boot="$boot $r"; done
	if [ "$boot" = " 10 11 13 14" ] && [ "$(tgapixels "$work/rel10-title.tga")" = "$(tgapixels "$work/rel11-title.tga")" ] &&
	   [ "$(tgapixels "$work/rel13-title.tga")" = "$(tgapixels "$work/rel14-title.tga")" ] &&
	   [ "$(tgapixels "$work/rel10-title.tga")" != "$(tgapixels "$work/rel14-title.tga")" ]; then
		report "releases:boot" PASS "1.0, 1.1, 1.3 and 1.4 each play from their own files; 1.0/1.1 and 1.3/1.4 share a title, the pairs differ"
	else
		report "releases:boot" FAIL "booted:$boot; titles $(for r in 10 11 13 14; do printf '%s ' "$(tgapixels "$work/rel$r-title.tga")"; done)"
	fi
	b10="$(bluer "$work/rel10-level3.tga")"; b11="$(bluer "$work/rel11-level3.tga")"; b13="$(bluer "$work/rel13-level3.tga")"; b14="$(bluer "$work/rel14-level3.tga")"
	if [ "$b10" -gt 0 ] && [ "$b11" -gt 0 ] && [ "$b13" -lt 0 ] && [ "$b14" -lt 0 ] &&
	   [ "$(tgapixels "$work/rel10-level3.tga")" = "$(tgapixels "$work/rel11-level3.tga")" ] &&
	   [ "$(tgapixels "$work/rel13-level3.tga")" = "$(tgapixels "$work/rel14-level3.tga")" ]; then
		report "releases:level-colours" PASS "level 3's dungeon is blue in 1.0 and 1.1, green in 1.3 and 1.4 (blue minus green: $b10 $b11 $b13 $b14)"
	else
		report "releases:level-colours" FAIL "blue minus green: $b10 $b11 $b13 $b14"
	fi
	# the guards' tables the game plays by, read by name: 1.0's, then the later ones
	gt() { awk '$1 == "init" { print $4, $5, $6, $7, $8, $9 }' "$work/rel$1.trace"; }
	if [ "$(gt 10)" = "61 16 61 16 0 0" ] && [ "$(gt 11)" = "75 20 75 20 0 0" ] &&
	   [ "$(gt 13)" = "75 20 75 20 1 4" ] && [ "$(gt 14)" = "75 20 75 20 1 4" ]; then
		report "releases:tables" PASS "strike, restrike, impaired block, refractory: 1.0's 61/16/61/16, 1.1-1.4's 75/20/75/20; level colours in 1.3/1.4 only"
	else
		report "releases:tables" FAIL "1.0 [$(gt 10)] 1.1 [$(gt 11)] 1.3 [$(gt 13)] 1.4 [$(gt 14)]"
	fi
	# the potions level's question: each release's own manual; 1.3 and 1.4 ask alike
	q10="$(tgapixels "$work/rel10-question.tga")"; q11="$(tgapixels "$work/rel11-question.tga")"
	q13="$(tgapixels "$work/rel13-question.tga")"; q14="$(tgapixels "$work/rel14-question.tga")"
	if [ "$(at "$work/rel10.trace" 480 7)" = "15" ] && [ "$q10" = "c6ce4c6e93c6d055" ] && [ "$q11" = "6b57b49150a35042" ] && [ "$q14" = "da132cb5c402f9c1" ] && [ "$q13" = "$q14" ]; then
		report "releases:copy-protection" PASS "1.0 asks WORD 4 LINE 6 PAGE 10, 1.1 PAGE 1 LINE 1 WORD 7, 1.3 and 1.4 PAGE 7 LINE 4 WORD 2 (build/gate/rel*-question.png)"
	else
		report "releases:copy-protection" FAIL "level $(at "$work/rel10.trace" 480 7); pixels $q10 $q11 $q13 $q14"
	fi
	for r in 10 11 13 14; do png "rel$r-question"; png "rel$r-level3"; done
	v10="$(strip "$work/rel10-version.tga")"; v11="$(strip "$work/rel11-version.tga")"; v13="$(strip "$work/rel13-version.tga")"; v14="$(strip "$work/rel14-version.tga")"
	if [ "$v10" = "0c0c281791bf882a" ] && [ "$v11" = "30e0e28a3b91e961" ] && [ "$v13" = "952712dec783cb59" ] && [ "$v14" = "df9f2bb94f524025" ]; then
		report "releases:version-line" PASS "Ctrl+V shows PRINCE OF PERSIA  V1.0, V1.1, V1.3, V1.4 - each release's own line"
	else
		report "releases:version-line" FAIL "strips $v10 $v11 $v13 $v14"
	fi
fi
# the game plays by those tables: skill 1's strike probability held at 0 and
# level 2's first guard never lands a blow
wd="$(workdir guards '{"enable_copyprot":false,"first_level":2}')"
boxed "$wd" --frames 800 --press 400:S:2 --press 520:L:30 --poke "520:Random Seed=0" --trace "$work/guards.trace" --trace-props "Kid.HP,Guard.Room" > /dev/null 2>&1
boxed "$wd" --frames 800 --press 400:S:2 --press 520:L:30 --poke "520:Random Seed=0" --freeze "0-799:Guard Skills.Strike Probability[1]=0" \
	--trace "$work/guards0.trace" --trace-props "Kid.HP,Guard.Room" > /dev/null 2>&1
if [ "$(at "$work/guards.trace" 799 1)" = "0" ] && [ "$(at "$work/guards0.trace" 799 1)" = "3" ] && [ "$(at "$work/guards0.trace" 799 2)" = "4" ]; then
	report "releases:tables-played" PASS "level 2's first guard kills the prince; with his skill's strike probability held at 0 the prince keeps 3 hit points"
else
	report "releases:tables-played" FAIL "HP $(at "$work/guards.trace" 799 1) / held $(at "$work/guards0.trace" 799 1), guard room $(at "$work/guards0.trace" 799 2)"
fi

# ------------------------------------------------------------------ 6d. the commands
ct="$work/commands.trace"
# the game's messages on its bottom line, as the pictures show them
msgs=""; for m in time version sound paused; do png "cmd-$m"; msgs="$msgs$(strip "$work/cmd-$m.tga") "; done
if [ "$msgs" = "1cec7b6cf820f05e a0d6dc697fd665b6 089b876412125279 27a495d1b6ce69bb " ]; then
	report "commands:messages" PASS "60 MINUTES LEFT, PRINCE OF PERSIA  V1.0, SOUND OFF, GAME PAUSED"
else
	report "commands:messages" FAIL "strips $msgs(build/gate/cmd-*.png)"
fi
# Pause holds the clock until a key; Restart Level puts the prince back where
# the level starts; Restart Game goes to the title; Next Level cuts the time
# to 15 minutes
if [ "$(at "$ct" 705 3)" = "$(at "$ct" 749 3)" ] && [ "$(at "$ct" 760 3)" -lt "$(at "$ct" 749 3)" ] &&
   [ "$(at "$ct" 779 14)" != "$(at "$ct" 759 14)" ] && [ "$(at "$ct" 790 14)" = "$(at "$ct" 759 14)" ] &&
   [ "$(at "$ct" 805 1)" = "65535" ] && [ "$(at "$ct" 1010 1)" = "3" ] && [ "$(at "$ct" 1110 2)" = "15" ] && [ "$(at "$ct" 1110 13)" = "4" ]; then
	report "commands:effects" PASS "paused 705-749 (ticks held at $(at "$ct" 705 3)); level 3 restarted at 780; title at 805; Next Level: level 4, 15 minutes"
else
	report "commands:effects" FAIL "ticks $(at "$ct" 705 3)/$(at "$ct" 749 3)/$(at "$ct" 760 3), x $(at "$ct" 759 14)/$(at "$ct" 779 14)/$(at "$ct" 790 14), level $(at "$ct" 805 1) $(at "$ct" 1010 1), minutes $(at "$ct" 1110 2), next $(at "$ct" 1110 13)"
fi
# Sound On/Off: no sound from the step after, through a cutscene's music
peaks="$(python3 - "$work/commands.pcm" "$ct" <<'PYEOF'
import struct, sys
pcm = open(sys.argv[1], "rb").read()
off = [0]
for line in open(sys.argv[2]):
    f = line.split()
    if f[0] == "init": continue
    num, den = map(int, f[1].split("/"))
    off.append(off[-1] + 44100 * den // num)
def peak(a, b):
    s = struct.unpack_from("<%dh" % ((off[b] - off[a]) * 2), pcm, off[a] * 4)
    return max(abs(x) for x in s) if s else 0
print(peak(400, 650), peak(651, len(off) - 1))
PYEOF
)"
set -- $peaks
if [ "${1:-0}" -gt 0 ] && [ "${2:-1}" = "0" ]; then
	report "commands:sound-off" PASS "peak $1 before Sound On/Off, silence after it to the end"
else
	report "commands:sound-off" FAIL "peaks before/after: $peaks"
fi

# ------------------------------------------------------------------ 6e. the cheats
cht="$work/cheats.trace"
# minutes, hit points, looking about, the screen flipped, feather fall,
# revived, next level without losing time, and the guard killed
looks="$(for s in 525 535 545 555 565 575; do printf '%s ' "$(at "$cht" $s 7)"; done)"
if [ "$(at "$cht" 465 2)" = "61" ] && [ "$(at "$cht" 475 2)" = "60" ] && [ "$(at "$cht" 485 5)" = "4" ] &&
   [ "$(at "$cht" 495 4)" = "2" ] && [ "$(at "$cht" 505 4)" = "3" ] && [ "$looks" = "5 1 2 1 5 1 " ] &&
   [ "$(at "$cht" 595 8)" != "0" ] && [ "$(at "$cht" 605 8)" = "0" ] && [ "$(at "$cht" 699 6)" -gt 0 ] && [ "$(at "$cht" 705 6)" = "-1" ] &&
   [ "$(at "$cht" 725 9)" != "0" ] && [ "$(at "$cht" 860 13)" = "2" ] && [ "$(at "$cht" 860 2)" = "60" ] &&
   [ "$(at "$cht" 1299 10)" = "-1" ] && [ "$(at "$cht" 1305 10)" = "0" ] && [ "$(at "$cht" 1305 11)" = "0" ]; then
	report "cheats:effects" PASS "+/- minutes, max and plain hit points, looks 5 1 2 1 5 1, flip, feather, revive, Next Level keeping 60 minutes, Kill Guard"
else
	report "cheats:effects" FAIL "min $(at "$cht" 465 2)/$(at "$cht" 475 2), HP $(at "$cht" 485 5) $(at "$cht" 495 4)->$(at "$cht" 505 4), looks $looks, flip $(at "$cht" 595 8)/$(at "$cht" 605 8), alive $(at "$cht" 699 6)->$(at "$cht" 705 6), feather $(at "$cht" 725 9), next $(at "$cht" 860 13) $(at "$cht" 860 2), guard $(at "$cht" 1299 10)->$(at "$cht" 1305 10)"
fi
png cheat-rooms; png cheat-corners; png cheat-blind
if [ "$(tgapixels "$work/cheat-blind.tga")" = "c1b748d95b4798ac" ] && [ "$(strip "$work/cheat-rooms.tga")" = "208535b7243f4e56" ] && [ "$(strip "$work/cheat-corners.tga")" = "0d22e58114f3c93d" ]; then
	report "cheats:pictures" PASS "Blind Mode leaves only the prince, the flames and his floor; Show Rooms prints S1 L5 R0 A0 B2, Show Corner Rooms AL0 AR0 BL6 BR3 (build/gate/cheat-*.png)"
else
	report "cheats:pictures" FAIL "blind $(tgapixels "$work/cheat-blind.tga"), strips $(strip "$work/cheat-rooms.tga") $(strip "$work/cheat-corners.tga")"
fi
# without the setting the cheats are no buttons at all: none is active, and
# pressing every one on the title - where any key would start a game -
# changes nothing
wd="$(workdir nocheats '{"enable_copyprot":false}')"
boxed "$wd" --frames 300 > "$work/nocheats-plain.txt" 2>/dev/null
boxed "$wd" --frames 300 --press "100:cC-+VKIW4:30" --press "100:682BHM5:30" > "$work/nocheats-pressed.txt" 2>/dev/null
if grep -qx 'activeButtons=15' "$work/nocheats-plain.txt" && grep -qx 'activeButtons=31' "$work/cheats.box.txt" &&
   cmp -s <(digests < "$work/nocheats-plain.txt") <(digests < "$work/nocheats-pressed.txt"); then
	report "cheats:off" PASS "15 buttons active without the setting (31 with it); every cheat held for 30 steps on the title changes nothing"
else
	report "cheats:off" FAIL "$(grep activeButtons "$work/nocheats-plain.txt") / $(grep activeButtons "$work/cheats.box.txt"); $(diff <(digests < "$work/nocheats-plain.txt") <(digests < "$work/nocheats-pressed.txt") | head -2 | tr '\n' ' ')"
fi

# ------------------------------------------------------------------ 6f. the slots
# the savestate: the run from it stands where the one that took it stood
snap="$(awk '$1 == 650 { $1 = $2 = $3 = ""; print }' "$work/makestate.trace")"
from="$(awk '$1 == 0 { $1 = $2 = $3 = ""; print }' "$work/fromstate.trace")"
if [ -n "$snap" ] && [ "$snap" = "$from" ] && [ "$(at "$work/fromstate.trace" 199 3)" != "$(at "$work/fromstate.trace" 0 3)" -o "$(at "$work/fromstate.trace" 199 4)" != "$(at "$work/fromstate.trace" 0 4)" ]; then
	report "slot:savestate" PASS "SDLPoP's quicksave of step 650 (level, room, place, HP, time): the run starts there and plays on"
else
	report "slot:savestate" FAIL "step 650 [$snap] vs the run's first [$from]"
fi
wd="$(workdir badstate '{}')"
printf 'V1.15   \0garbage' > "$wd/OLD.SAV"; printf '{"savestate":["OLD.SAV"]}' > "$wd/slots"
boxed "$wd" --frames 1 > "$work/bs1.txt" 2>/dev/null
head -c 100 "$work/QUICKSAVE.SAV" > "$wd/OLD.SAV"
boxed "$wd" --frames 1 > "$work/bs2.txt" 2>/dev/null
if grep -q "^loadError=the project's savestate OLD.SAV is not an SDLPoP quicksave" "$work/bs1.txt" &&
   grep -q "^loadError=the project's savestate OLD.SAV is 100 bytes; a quicksave of this SDLPoP" "$work/bs2.txt"; then
	report "slot:savestate-refused" PASS "another header, and a quicksave of another size (another SDLPoP build), are refused by name"
else
	report "slot:savestate-refused" FAIL "$(grep -m1 . "$work/bs1.txt") / $(grep -m1 . "$work/bs2.txt")"
fi
# the hall of fame's name is the player_name setting, entered by the game
# itself: level 14 poked in, its two gates made floor, the prince runs left
# to the princess, and the won game's time goes in under the name
wd="$(workdir hofname '{"enable_copyprot":false,"player_name":"Sergio"}')"
boxed "$wd" --frames 3300 --press 400:S:2 --poke "420:Level.Next=14" --poke "470:Room 3.Tile[19]=1" --poke "470:Room 1.Tile[19]=1" \
	--press 480:L:400 --dump-domain "Hall of Fame" "$work/hof.bin" --screenshot "2799:$work/hofname.tga" \
	--trace "$work/hofname.trace" --trace-props "Level.Current,Hall of Fame.Minutes[0]" > /dev/null 2>&1
png hofname
wd="$(workdir hofempty '{"player_name":" "}')"
boxed "$wd" --frames 1 > "$work/hofempty.txt" 2>/dev/null
if [ "$(head -c 25 "$work/hof.bin" 2>/dev/null | tr -d '\0')" = "Sergio" ] && [ "$(at "$work/hofname.trace" 3299 2)" = "60" ] &&
   grep -q "^loadError=the Player Name (Hall of Fame) setting has nothing the game can show" "$work/hofempty.txt"; then
	report "settings:player-name" PASS "a won game (60 minutes left) enters Sergio in the hall of fame by itself (build/gate/hofname.png); a blank name is refused"
else
	report "settings:player-name" FAIL "name [$(head -c 25 "$work/hof.bin" 2>/dev/null | tr -d '\0')], minutes $(at "$work/hofname.trace" 3299 2); $(grep -m1 . "$work/hofempty.txt")"
fi

# ------------------------------------------------------------------ 6g. the in-game time
# Time.IGT Ticks counts what the game's clock counts down, and Time.IGT Ms is
# that at 12 ticks a second (the table's gameTimer): on JaffarPlus's route,
# with the clock poked to 3 ticks before a minute ends, it gains exactly one
# tick a step of play through the minute's rollover (a minute is 719 ticks),
# goes on through the level's closing music, and stands still in the
# princess's cutscene
wd="$(workdir igt '{"skip_title":true,"enable_copyprot":false}')"
boxed "$wd" --frames 1100 --movie "$route" --poke "0:Random Seed=0" --poke "50:Time.Ticks Left=3" --trace "$work/igt.trace" \
	--trace-props "Level.Current,Time.Minutes Left,Time.Ticks Left,Time.IGT Ticks,Time.IGT Ms" > /dev/null 2>&1
igt="$(awk '$1 ~ /^[0-9]+$/ && $1 >= 51 && $1 <= 273 && $7 - p != 1 { odd++ } $1 ~ /^[0-9]+$/ && $8 != int($7 * 1000 / 12) { bad++ } { p = $7 } END { print odd + 0, bad + 0 }' "$work/igt.trace")"
if [ "$igt" = "0 0" ] && [ "$(at "$work/igt.trace" 51 2)" = "60" ] && [ "$(at "$work/igt.trace" 53 2)" = "59" ] &&
   [ "$(at "$work/igt.trace" 273 4)" = "940" ] && [ "$(at "$work/igt.trace" 412 4)" = "1079" ] && [ "$(at "$work/igt.trace" 1000 4)" = "1079" ] &&
   [ "$(at "$work/igt.trace" 1099 4)" = "1174" ] && [ "$(at "$work/igt.trace" 1099 5)" = "97833" ]; then
	report "time:igt" PASS "one tick a step through the minute's rollover (60 -> 59), 940 at the exit door, still in the cutscene, 1174 = 01:37.833 in level 2"
else
	report "time:igt" FAIL "off-by-a-tick steps and ms mismatches: $igt; minutes $(at "$work/igt.trace" 51 2)->$(at "$work/igt.trace" 53 2); IGT $(at "$work/igt.trace" 273 4) $(at "$work/igt.trace" 412 4) $(at "$work/igt.trace" 1000 4) $(at "$work/igt.trace" 1099 4)"
fi

# ------------------------------------------------------------------ 7. the package
if sh "$here/build-package.sh" -m "$mb" -o "$work/pkg1" > "$work/pkg1.log" 2>&1 &&
   sh "$here/build-package.sh" -m "$mb" -o "$work/pkg2" > "$work/pkg2.log" 2>&1 &&
   cmp -s "$work/pkg1/sdlpop.chimeraCore" "$work/pkg2/sdlpop.chimeraCore"; then
	report "package" PASS "$(grep 'package sha1' "$work/pkg1.log"), the same twice"
else
	report "package" FAIL "see build/gate/pkg*.log"
fi

echo
echo "$ok ok, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ]
