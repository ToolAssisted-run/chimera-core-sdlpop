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
#   - take its settings (a first level, the minutes, the hit points arrive)
#   - refuse a missing data file, a later release's file and a damaged one
#   - package deterministically
#
# The data is the user's Prince of Persia 1.0, never in the repository: the
# gate takes it from tests/roms-local/pop10 (or -d <dir>). Without it only the
# build, the declarations and the refusal of a project with no data run.
#
# Usage: ./run-gate.sh [-q] [-m <miniBox dir>] [-d <PoP 1.0 dir>]
#   -q skips the build (uses what is built)
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
mb="${MINIBOX_DIR:-$HOME/chimera/extern/chimera-common-minibox}"
data="${POP10_DIR:-$root/tests/roms-local/pop10}"
quick=0
while getopts "qm:d:" opt; do
	case "$opt" in
		q) quick=1 ;;
		m) mb="$OPTARG" ;;
		d) data="$OPTARG" ;;
		*) exit 2 ;;
	esac
done
mb="$(cd "$mb" 2>/dev/null && pwd)" || { echo "miniBox not found; pass -m or set MINIBOX_DIR" >&2; exit 1; }

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

# a work dir: the data files the project would mount, and a settings file
workdir() {
	local wd="$work/$1"
	mkdir -p "$wd"
	cp "$data"/*.DAT "$wd/" 2>/dev/null
	rm -f "$wd/CONFIG.DAT" "$wd/SETUP.DAT"
	printf '%s' "$2" > "$wd/settings"
	echo "$wd"
}

# ------------------------------------------------------------------ 3. no data
wd="$work/nodata"; mkdir -p "$wd"; printf '{}' > "$wd/settings"
boxed "$wd" --frames 1 > "$work/nodata.txt" 2>/dev/null
if grep -q '^loadError=Prince of Persia needs PRINCE.DAT, KID.DAT, .*MIDISND2.DAT - add them as the project.s firmware' "$work/nodata.txt"; then
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
if grep -qx 'loadError=Prince of Persia needs KID.DAT - add it as the project.s firmware.' "$work/r1.txt" && cmp -s <(grep loadError "$work/r1.txt") <(grep loadError "$work/r1n.txt"); then
	report "refuse:missing-file" PASS "$(sed -n 's/^loadError=//p' "$work/r1.txt")"
else
	report "refuse:missing-file" FAIL "$(grep -m1 . "$work/r1.txt")"
fi

wd="$(workdir refuse-release '{}')"; cp "$root/extern/SDLPoP/data/DIGISND1.DAT" "$wd/DIGISND1.DAT"
boxed "$wd" --frames 1 > "$work/r2.txt" 2>/dev/null
if grep -q "^loadError=DIGISND1.DAT is a later release's (as SDLPoP ships it), not Prince of Persia 1.0's" "$work/r2.txt"; then
	report "refuse:later-release" PASS "SDLPoP's own DIGISND1.DAT refused by name"
else
	report "refuse:later-release" FAIL "$(grep -m1 . "$work/r2.txt")"
fi

wd="$(workdir refuse-damaged '{}')"; printf '\x55' | dd of="$wd/LEVELS.DAT" bs=1 seek=1000 conv=notrunc 2>/dev/null
boxed "$wd" --frames 1 > "$work/r3.txt" 2>/dev/null
if grep -q "^loadError=LEVELS.DAT is not Prince of Persia 1.0's (37031 bytes, SHA-1 " "$work/r3.txt"; then
	report "refuse:damaged-file" PASS "one byte changed in LEVELS.DAT: refused with both hashes"
else
	report "refuse:damaged-file" FAIL "$(grep -m1 . "$work/r3.txt")"
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
# name, steps, settings; the arguments are test_args's
tests=(
	"title|700|{}"
	"route|1100|{\"skip_title\":true,\"enable_copyprot\":false}"
	"copyprot|1150|{\"skip_title\":true}"
	"speaker|500|{\"sound\":\"pcSpeaker\"}"
	"fixes|420|{\"skip_title\":true,\"enable_copyprot\":false,\"use_fixes_and_enhancements\":true}"
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
		# the same route with copy protection: the potions level and its question
		copyprot) args=(--movie "$route" --poke "0:Random Seed=0" --press 1110:R:2 --screenshot "1100:$work/manual.tga") ;;
		speaker) args=(--press 400:S:2) ;;
	esac
}
for t in "${tests[@]}"; do
	IFS='|' read -r name frames settings <<< "$t"
	wd="$(workdir "$name" "$settings")"
	test_args "$name"
	args+=(--frames "$frames")
	# the trace the legs below read is the first sandboxed run's; the native
	# run keeps its own, which has to be the same
	trace=(--trace-props "$props" --trace)

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
for shot in "title:299 (Broderbund presents):69546642de679025" "level1:450 (level 1, room 1):736ecb4c481d715d" \
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
if [ "$(at "$tr" 272 2)" = "1" ] && [ "$(at "$tr" 273 2)" = "2" ] && [ "$(at "$tr" 273 3)" = "9" ] && [ "$(at "$tr" 273 6)" = "3" ]; then
	report "route:level-1" PASS "the prince leaves level 1 through room 9 with 3 hit points, Level.Next 2 from step 273"
else
	report "route:level-1" FAIL "at 272/273: next level $(at "$tr" 272 2)/$(at "$tr" 273 2), room $(at "$tr" 273 3), HP $(at "$tr" 273 6)"
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
