#!/bin/bash
# The frontend half of the gate: the package, as Chimera loads it.
#
# - chimera-run (the engine, no frontend): JaffarPlus's level 1 route from a
#   movie file and from a hand-written project with no files, and a project
#   with a level set in its "levels" slot. The Game State and Level domains and
#   the last picture must be the native reference's.
# - Chimera itself, headless under Mono on a private Xvfb: the same project
#   opened and played through Lua, its domains compared with the native
#   reference; the property library (game.list/get/set) tried against the
#   same bytes when this Chimera has it; the core's own refusal reaching the
#   frontend; the package's keybinds becoming the defaults.
#
# Nothing is written under the Chimera checkout: the config, the data home,
# the Base path and the sandbox's log are all in build/frontend.
#
# Usage: ./run-frontend.sh [--chimera-root <path>] [-m <miniBox dir>] [-d <PoP 1.0 dir>]
set -u

here="$(cd "$(dirname "$0")" && pwd)"
wb="$(cd "$here/.." && pwd)"
root="$(cd "$wb/.." && pwd)"
chimera_root=""
mb="${MINIBOX_DIR:-}"
data="${POP10_DIR:-$root/tests/roms-local/pop10}"
while [ $# -gt 0 ]; do
	case "$1" in
		--chimera-root) chimera_root="$2"; shift ;;
		-m) mb="$2"; shift ;;
		-d) data="$2"; shift ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done
if [ -z "$chimera_root" ]; then
	for candidate in "$root/../chimera" "$HOME/chimera"; do
		[ -d "$candidate" ] && { chimera_root="$candidate"; break; }
	done
fi
[ -n "$chimera_root" ] && [ -d "$chimera_root" ] || { echo "chimera checkout not found; pass --chimera-root" >&2; exit 1; }
chimera_root="$(cd "$chimera_root" && pwd)"
[ -n "$mb" ] || mb="$chimera_root/extern/chimera-common-minibox"

emu_exe="$chimera_root/build/Chimera.exe"
crun="$chimera_root/build/dll/chimera-run"
rn="$root/build/native/run-native"
route="$wb/tests/lvl01-route.txt"
work="$root/build/frontend"
rm -rf "$work"
mkdir -p "$work/data"

ok=0
failed=0
skipped=0
report() {
	printf "%-26s %-6s %s\n" "$1" "$2" "$3"
	case "$2" in PASS) ok=$((ok+1)) ;; SKIP) skipped=$((skipped+1)) ;; *) failed=$((failed+1)) ;; esac
}
printf "%-26s %-6s %s\n" "Check" "Result" "Detail"
printf "%-26s %-6s %s\n" "-----" "------" "------"

[ -f "$data/PRINCE.DAT" ] || { echo "no Prince of Persia 1.0 data in $data: nothing for the frontend to play, skipping"; exit 0; }
[ -x "$rn" ] || { echo "native reference not built (make -f native.mk)" >&2; exit 1; }

# ---------------------------------------------------------------- the package
if sh "$wb/build-package.sh" -m "$mb" -o "$work/pkg" > "$work/package.log" 2>&1; then
	report "package" PASS "$(grep 'package sha1' "$work/package.log")"
else
	report "package" FAIL "see build/frontend/package.log"; echo; echo "$ok ok, $failed failed"; exit 1
fi
pkg="$work/pkg/sdlpop.chimeraCore"

# the data files, as --firmware id=path, and as a work dir for the reference:
# the ones the package declares for 1.0 (its PRINCE.EXE too; not the CGA and
# EGA files the release also has)
firmware=()
mkdir -p "$work/native"
for b in $(python3 -c "
import json, zipfile
cfg = json.loads(zipfile.ZipFile('$pkg').read('waterbox.config'))
print(' '.join(sorted({f['name'] for f in cfg['firmware'] if '1.0' in json.dumps(f.get('requiredWhen', {}))})))"); do
	[ -f "$data/$b" ] || continue
	firmware+=("$b=$data/$b")
	cp "$data/$b" "$work/native/"
done
settings='{"skip_title":true,"enable_copyprot":false}'
printf '%s' "$settings" > "$work/native/settings"
frames=274

# the native reference: the route, its domains after the last row and its last picture
"$rn" "$work/native" --frames "$frames" --movie "$route" --dump-domain "Game State" "$work/native.gs.bin" \
	--screenshot "$((frames - 1)):$work/native.last.tga" > /dev/null 2>&1
"$rn" "$work/native" --frames "$frames" --movie "$route" --dump-domain "Level" "$work/native.level.bin" > /dev/null 2>&1

same() { cmp -s "$work/native.gs.bin" "$1" && cmp -s "$work/native.level.bin" "$2"; }

# ---------------------------------------------------------------- chimera-run
if [ ! -x "$crun" ]; then
	report "engine" SKIP "no chimera-run at $crun"
else
	fwargs=(); for f in "${firmware[@]}"; do fwargs+=(--firmware "$f"); done
	python3 "$here/make-project.py" "$pkg" "$work/route.chimeraProject" "$frames" --movie "$route" \
		--settings "$settings" --log-out "$work/route.log"
	: > "$work/empty.rom"
	( cd "$work" && "$crun" "$pkg" empty.rom route.log --settings "$settings" "${fwargs[@]}" \
		--dump "Game State=$work/run.gs.bin" --dump "Level=$work/run.level.bin" \
		--screenshot "$((frames - 1))=$work/run.last.tga" --meta "$work/run.meta" > "$work/run.log" 2>&1 )
	if grep -q '^status=OK' "$work/run.meta" 2>/dev/null && same "$work/run.gs.bin" "$work/run.level.bin" &&
	   python3 "$here/compare-pictures.py" "$work/native.last.tga" "$work/run.last.tga" > "$work/run.pic" 2>&1; then
		report "engine:route" PASS "$frames frames from a movie file: Game State, Level and the last picture are the reference's"
	else
		report "engine:route" FAIL "$(grep -m1 detail= "$work/run.meta" 2>/dev/null) $(cat "$work/run.pic" 2>/dev/null) (build/frontend/run.log)"
	fi

	( cd "$work" && "$crun" --project "$work/route.chimeraProject" "$pkg" "${fwargs[@]}" \
		--dump "Game State=$work/proj.gs.bin" --dump "Level=$work/proj.level.bin" --meta "$work/proj.meta" > "$work/proj.log" 2>&1 )
	if grep -q '^status=OK' "$work/proj.meta" 2>/dev/null && same "$work/proj.gs.bin" "$work/proj.level.bin"; then
		report "engine:project" PASS "a project with no files: the settings and the movie from it, the same machine"
	else
		report "engine:project" FAIL "$(grep -m1 detail= "$work/proj.meta" 2>/dev/null) (build/frontend/proj.log)"
	fi

	# a level set in the "levels" slot: room 1's tile 10 made plain floor
	mkdir -p "$work/levels"
	python3 "$here/make-levels.py" "$data/LEVELS.DAT" "$work/levels/MYLEVELS.DAT" 1 1 10 1
	python3 "$here/make-project.py" "$pkg" "$work/levels.chimeraProject" 10 --settings "$settings" \
		--file "levels=$work/levels/MYLEVELS.DAT"
	lfw=(); for f in "${firmware[@]}"; do case "$f" in LEVELS.DAT=*) ;; *) lfw+=(--firmware "$f") ;; esac; done
	( cd "$work" && "$crun" --project "$work/levels.chimeraProject" "$pkg" --files "$work/levels" "${lfw[@]}" \
		--dump "Level=$work/levels.level.bin" --meta "$work/levels.meta" > "$work/levels.log" 2>&1 )
	tile="$(od -An -tu1 -j10 -N1 "$work/levels.level.bin" 2>/dev/null | tr -d ' ')"
	orig="$(od -An -tu1 -j10 -N1 "$work/native.level.bin" 2>/dev/null | tr -d ' ')"
	if grep -q '^status=OK' "$work/levels.meta" 2>/dev/null && [ "$tile" = "$(( (orig & 224) | 1 ))" ]; then
		report "engine:levels-slot" PASS "MYLEVELS.DAT from the project's slot, no LEVELS.DAT: room 1 tile 10 is byte $tile (was $orig)"
	else
		report "engine:levels-slot" FAIL "tile byte ${tile:-?} (original ${orig:-?}): $(grep -m1 detail= "$work/levels.meta" 2>/dev/null)"
	fi
fi

# ---------------------------------------------------------------- Chimera itself
if [ ! -f "$emu_exe" ]; then
	report "frontend" SKIP "Chimera not built: $emu_exe"
	echo; echo "$ok ok, $failed failed, $skipped skipped"; [ "$failed" -eq 0 ]; exit
fi
export LD_LIBRARY_PATH="$chimera_root/build/dll:$chimera_root/build:/usr/lib/x86_64-linux-gnu"
export MONO_CRASH_NOFILE=1 MONO_WINFORMS_XIM_STYLE=disabled ALSOFT_DRIVERS=null
export CHIMERA_DATA_HOME="$work/data" MINIBOX_LOG="$work/minibox.log"
xvfb_pid=""
cleanup() { [ -n "$xvfb_pid" ] && kill "$xvfb_pid" 2>/dev/null; }
trap cleanup EXIT
if [ -z "${DISPLAY:-}" ]; then
	command -v Xvfb > /dev/null || { report "frontend" SKIP "no Xvfb"; echo; echo "$ok ok, $failed failed, $skipped skipped"; exit 0; }
	for n in 90 91 92 93 94 95 96 97 98 99; do
		if [ ! -e "/tmp/.X11-unix/X$n" ]; then
			Xvfb ":$n" -screen 0 640x480x24 -nolisten tcp > "$work/xvfb.log" 2>&1 & xvfb_pid=$!
			export DISPLAY=":$n"; break
		fi
	done
	sleep 1
fi

# a config of its own, with the Base path (and so every folder Chimera
# writes by default) in the work dir
config="$work/config.ini"
echo 'client.exit()' > "$work/exit.lua"
( cd "$work" && timeout 120 mono "$emu_exe" --headless "--config=$config" "--lua=$work/exit.lua" ) > "$work/bootstrap.log" 2>&1
python3 - "$config" "$work" <<'EOF'
import json, sys
path, base = sys.argv[1], sys.argv[2]
c = json.load(open(path))
for e in c["PathEntries"]["Paths"]:
    if e["Type"] == "Base" and e["System"] == "Global_NULL":
        e["Path"] = base
json.dump(c, open(path, "w"), indent=2)
EOF

gui() {
	# gui <tag> <config> <project> <frames> [firmware...]
	local tag="$1" cfg="$2" project="$3" nframes="$4"; shift 4
	local fw=(); for f in "$@"; do fw+=("--firmware=$f"); done
	mkdir -p "$work/$tag"
	printf 'frames=%s\nout=%s\nmeta=%s\nshot=%s\n' "$nframes" "$work/$tag" "$work/$tag/meta.txt" "$work/$tag/shot.png" > "$work/$tag/job.txt"
	( cd "$work" && MINIHAWK_JOB="$work/$tag/job.txt" timeout 300 mono "$emu_exe" --headless "--config=$cfg" \
		"--core=$pkg" "${fw[@]}" "--project=$project" "--lua=$here/frontend.lua" ) > "$work/$tag.log" 2>&1
	echo $?
}
meta() { sed -n "s/^$2=//p" "$work/$1/meta.txt" 2>/dev/null; }

cp "$work/route.chimeraProject" "$work/gui.chimeraProject"
gui gui "$config" "$work/gui.chimeraProject" "$frames" "${firmware[@]}" > /dev/null
if [ "$(meta gui status)" = "OK" ] && same "$work/gui/gamestate.bin" "$work/gui/level.bin"; then
	report "gui:project" PASS "the project opened in Chimera: after $(meta gui frames) frames ($(meta gui lag) lag) Game State and Level are the reference's"
else
	report "gui:project" FAIL "status $(meta gui status) $(meta gui detail) (build/frontend/gui.log)"
fi

# with the cheats setting the project has the cheats' 16 columns as well (33),
# and one of them pressed - More Time on row 50 - reaches the game: the route
# ends with 61 minutes, as the reference pressing the same key does
mkdir -p "$work/nativecheats"; cp "$work/native/"* "$work/nativecheats/"
csettings='{"skip_title":true,"enable_copyprot":false,"cheats":true}'
printf '%s' "$csettings" > "$work/nativecheats/settings"
"$rn" "$work/nativecheats" --frames "$frames" --movie "$route" --press 50:+:1 --dump-domain "Game State" "$work/cheats.gs.bin" > /dev/null 2>&1
python3 "$here/make-project.py" "$pkg" "$work/cheats.chimeraProject" "$frames" --movie "$route" \
	--settings "$csettings" --press "50:Cheat More Time"
gui cheatsgui "$config" "$work/cheats.chimeraProject" "$frames" "${firmware[@]}" > /dev/null
ncols="$(python3 -c "import json; p = json.load(open('$work/cheats.chimeraProject')); print(p['input'].splitlines()[1].count('|'))")"
if [ "$(meta cheatsgui status)" = "OK" ] && [ "$ncols" = "33" ] && cmp -s "$work/cheats.gs.bin" "$work/cheatsgui/gamestate.bin" &&
   ! cmp -s "$work/native.gs.bin" "$work/cheats.gs.bin"; then
	report "gui:cheats-project" PASS "a project with the cheats on ($ncols columns): More Time on row 50 reaches the game, Game State is the reference's"
else
	report "gui:cheats-project" FAIL "status $(meta cheatsgui status) $(meta cheatsgui detail), $ncols columns (build/frontend/cheatsgui.log)"
fi

# the property library, where this Chimera has it: the table is the one the
# core exports, a value read by name is the byte in the domain, a bit field
# element reads, a set takes, and a name the core does not have is nil
nprops="$("$rn" "$work/native" --frames 1 --props-json "$work/props.json" > /dev/null 2>&1; python3 -c "import json; print(len(json.load(open('$work/props.json'))['properties']))")"
if [ "$(meta gui game_list)" = "absent" ]; then
	report "gui:game-properties" SKIP "this Chimera has no game.* library"
elif [ "$(meta gui game_list)" = "$nprops" ] && [ "$(meta gui game_kid_x)" = "$(meta gui game_kid_x_byte)" ] &&
     [ "$(meta gui game_level_next)" = "2" ] && [ "$(meta gui game_tile)" = "19" ] &&
     [ "$(meta gui game_set)" = "true" ] && [ "$(meta gui game_kid_x_after_set)" = "120" ] && [ "$(meta gui game_unknown)" = "nil" ]; then
	report "gui:game-properties" PASS "game.list() has the core's $nprops; Kid.X by name is its byte; Room 1.Tile[10] = 19; set Kid.X 120 took"
else
	report "gui:game-properties" FAIL "list $(meta gui game_list)/$nprops, Kid.X $(meta gui game_kid_x)/$(meta gui game_kid_x_byte), tile $(meta gui game_tile), set $(meta gui game_set)->$(meta gui game_kid_x_after_set) $(meta gui game_error)"
fi

# the core's own refusal reaches the frontend: a project that pins no data
# (so Chimera itself asks nothing) given SDLPoP's own DIGISND1.DAT
python3 - "$work/gui.chimeraProject" "$work/nopins.chimeraProject" <<'EOF'
import json, sys
p = json.load(open(sys.argv[1]))
p["firmware"] = []
json.dump(p, open(sys.argv[2], "w"), indent="\t")
EOF
bad=(); for f in "${firmware[@]}"; do case "$f" in DIGISND1.DAT=*) bad+=("DIGISND1.DAT=$root/extern/SDLPoP/data/DIGISND1.DAT") ;; *) bad+=("$f") ;; esac; done
code="$(gui refuse "$config" "$work/nopins.chimeraProject" 10 "${bad[@]}")"
if [ "$code" = "64" ] && grep -q "DIGISND1.DAT is the one SDLPoP ships, not Prince of Persia 1.0's" "$work/refuse.log"; then
	report "gui:refusal" PASS "the core's load error is what Chimera shows (headless: exit 64 with the text)"
else
	report "gui:refusal" FAIL "exit $code; $(grep -m1 -i 'DIGISND1\|error' "$work/refuse.log" | cut -c1-100)"
fi

# the bindings the package ships become the frontend's defaults
python3 "$here/forget-controller.py" "$config" "$work/config.keys.ini" "Prince of Persia"
gui keys "$work/config.keys.ini" "$work/gui.chimeraProject" 1 "${firmware[@]}" > /dev/null
if [ "$(meta keys status)" = "OK" ] && python3 "$here/check-keybinds.py" "$work/config.keys.ini" "$wb/default_keybinds.json" "Prince of Persia" > "$work/keys.txt" 2>&1; then
	report "gui:keybinds" PASS "$(cat "$work/keys.txt")"
else
	report "gui:keybinds" FAIL "$(head -1 "$work/keys.txt" 2>/dev/null) (build/frontend/keys.log)"
fi

# nothing written under the Chimera checkout
if [ -z "$(find "$chimera_root/build" -maxdepth 2 -newer "$work/exit.lua" \( -name 'config*.ini' -o -name 'Movies' -o -name 'Firmware' -o -name 'CoreCache' -o -name 'ChimeraMono_last*' \) 2>/dev/null)" ]; then
	report "gui:writes-nothing-home" PASS "config, data home, Base path and logs all in build/frontend"
else
	report "gui:writes-nothing-home" FAIL "$(find "$chimera_root/build" -maxdepth 2 -newer "$work/exit.lua" | head -3 | tr '\n' ' ')"
fi

echo
echo "$ok ok, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ]
