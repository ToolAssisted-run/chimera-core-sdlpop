# Building the SDLPoP core

This repository builds SDLPoP, the open-source port of Prince of Persia, as a
Chimera game core: one game rebuilt as a core, not an emulator. The result is
one file, `sdlpop.chimeraCore`, which Chimera loads. The steps below are the
ones the repository's CI runs from a fresh clone
(`.github/workflows/chimera.yml`).

Placeholders used in this document:

- `<core>`: the checkout of this repository.
- `<chimera>`: a checkout of Chimera
  (https://github.com/ToolAssisted-run/chimera).
- `<minibox>`: `<chimera>/extern/chimera-common-minibox`, the miniBox
  submodule of that checkout. miniBox is the sandbox host and the guest
  toolchain.

## Requirements

- Linux, x86_64. CI builds on GitHub's `ubuntu-latest` runner. Cores are built
  on Linux; the package that comes out runs on Linux and on Windows.
- git, and the packages CI installs:

  ```
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends meson ninja-build build-essential cmake pkg-config python3 mono-complete xvfb libgl1-mesa-dev libegl-dev libx11-dev libxext-dev libasound2-dev
  ```

- The compiler is the distribution's gcc (`build-essential`). The workflow
  pins no compiler version.
- The .NET SDK 8.0. CI sets it up with `actions/setup-dotnet@v4`,
  `dotnet-version: '8.0'`. It builds Chimera and runs Chimera's contract
  tests; the core's own build does not use it. Chimera's README installs
  Microsoft's SDK with
  `curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0`
  and says a distribution-built SDK lacks targets the frontend needs.

Nothing in this repository's scripts downloads anything. SDL 2 and SDLPoP are
compiled from the two submodules. miniBox builds the guest C library (musl)
and its `musl-gcc` wrapper from sources in its own checkout.

## Get the sources

CI checks out both repositories with `actions/checkout@v6` and
`submodules: recursive`, Chimera at its `main` branch. By hand:

```
git clone --recursive https://github.com/ToolAssisted-run/chimera-core-sdlpop.git <core>
git clone --recursive https://github.com/ToolAssisted-run/chimera.git <chimera>
```

In a clone made without `--recursive`:

```
git submodule update --init --recursive
```

This repository has two submodules: `extern/SDLPoP` (the game) and
`extern/SDL` (SDL 2, marked shallow in `.gitmodules`).

The Chimera checkout can be anywhere. CI puts it in `chimera-checkout` inside
the core checkout. The scripts find miniBox in this order:

1. `-m <miniBox dir>` on the script's command line (`MB=<dir>` for the
   makefiles).
2. The `MINIBOX_DIR` environment variable. CI sets it.
3. `<chimera root>/extern/chimera-common-minibox`, for
   `build-package.sh -r <chimera root>` only.
4. `~/chimera/extern/chimera-common-minibox`.

The rest of this document sets the variable once:

```
export MINIBOX_DIR=<chimera>/extern/chimera-common-minibox
```

## Build miniBox

The workflow's commands:

```
mb=<chimera>/extern/chimera-common-minibox
[ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
meson compile -C "$mb/build/meson-linux"
```

This builds the two things the core needs, both under
`<minibox>/build/meson-linux`:

- `musl-gcc`, the C guest toolchain that `waterbox/guest.mk` compiles with.
- `source/host/libminiboxhost.so`, the sandbox host library that the
  `run-wbx` harness links.

CI keeps `build/meson-linux` between runs with `actions/cache@v4`. By hand the
directory simply stays, and the first line above skips `meson setup` when it
is already there.

## Build the core

### Patches

`patches/` holds three numbered patches for `extern/SDLPoP`.
`waterbox/apply-patches.sh` applies them. Both makefiles run it before any
SDLPoP file is compiled (`waterbox/sources.mk`, the `build/patches.stamp`
rule), so there is no separate step. It can also be run by hand:

```
./waterbox/apply-patches.sh
```

The script judges the series as a whole:

- It first applies the whole series to a scratch copy of the touched files as
  the submodule's HEAD has them. If that fails it stops and names the patch.
- A pristine tree gets every patch applied (`applied: <patch>`).
- A tree that is exactly what the series leaves behind is left alone
  (`already applied: all 3 patches`).
- Anything in between is an error that names the files.

After a build `git status` shows `extern/SDLPoP` as modified. That is the
applied series. Do not commit it.

### The native reference and the guest

```
cd <core>
make -C waterbox -f native.mk -j"$(nproc)"
make -C waterbox -f guest.mk -j"$(nproc)"
```

- `native.mk` builds the native reference with the host's gcc: the same SDL,
  SDLPoP and core sources as the guest. It makes `build/native/run-native`,
  which drives the core's exports directly, and `build/native/run-wbx`, which
  drives `core.wbx` through the miniBox host as the frontend does. The gate
  compares the two.
- `guest.mk` builds `build/guest/core.wbx` with miniBox's `musl-gcc`. The
  file counts as built only after miniBox's `check-wbx.sh` passes it (no
  thread-local storage, no `%fs`, no red zone).

Both makefiles take `MB=<miniBox dir>`; without it they use `MINIBOX_DIR`.
CI does not call them directly: `waterbox/run-gate.sh` runs both, and
`waterbox/build-package.sh` runs `guest.mk` again.

## Build the package

```
./waterbox/build-package.sh -r <chimera>
```

Usage: `./waterbox/build-package.sh [-m <miniBox dir>] [-r <chimera root>] [-o <out dir>]`

- `-r <chimera root>` writes `<chimera root>/build/Cores/sdlpop.chimeraCore`
  and removes any `<chimera root>/build/CoreCache/sdlpop-*` directory. This is
  what CI runs.
- `-o <out dir>` writes `<out dir>/sdlpop.chimeraCore` instead.
- With neither, the file is `build/package/sdlpop.chimeraCore`.
- `-m <miniBox dir>` names miniBox and overrides `MINIBOX_DIR`.

The script runs `guest.mk` (log: `build/package-make.log`), checks
`core.wbx`, and packs it with `waterbox/waterbox.config`,
`waterbox/default_keybinds.json`, `waterbox/file_slots.json`, the licences
(from `waterbox/package-licenses.json`) and a `build.json` that records what
built it. It writes the archive twice and stops if the two differ. On success
it prints `package sha1 <hash>` and `packaged -> <path>`.

The package's version is the commit it was built from:

- CI passes `CORE_VERSION` (the commit). That value is stamped as it is.
- Without `CORE_VERSION` the script stamps `<commit>+local`, and
  `<commit>-dirty+local` when tracked files differ from HEAD. The patched
  `extern/SDLPoP` tree does not count as a change.
- The commit's date, in UTC, is stamped beside it as `versionDate`.

A package built by hand is for testing. Chimera's publish script refuses a
version that carries `+local` or `-dirty`.

The script builds the guest itself, so it also works on a fresh clone with
nothing built yet; its build log is `build/package-make.log`.

## Install it into Chimera

Chimera includes no cores and downloads nothing: it has no network code. A
core gets into Chimera as a file somebody puts in its `Cores` folder.

- In a Chimera source checkout the folder is `<chimera>/build/Cores/`.
  `./waterbox/build-package.sh -r <chimera>` writes the package straight
  there.
- In a release bundle the folder is `Cores`, beside `Chimera.exe`. Copy
  `sdlpop.chimeraCore` into it. Another folder can be chosen in
  File > Core Manager > Change folder...
- File > Core Manager lists what is in the folder. Refresh List rescans it
  for a package copied in while the window is open.

The same package file works on Linux and on Windows. Chimera's sandbox,
miniBox, runs the guest inside it on either.

Without building: this repository's CI publishes the package on its Releases
page (https://github.com/ToolAssisted-run/chimera-core-sdlpop/releases). A
rolling `dev` release follows every green push to `main`. A dated
`nightly-YYYY-MM-DD` release comes from the scheduled run (04:00 UTC), only
when `main` moved since the last one. A published package is named
`sdlpop-<version>.chimeraCore`.

In Chimera a new project picks the core in File > New Project... (Kind: Game)
and the release in the wizard's System box.

## Run the gates

### The core gate

```
./waterbox/run-gate.sh
```

Usage: `./waterbox/run-gate.sh [-q] [-m <miniBox dir>] [-d <dir with pop10 pop11 pop13 pop14>]`

- `-q` skips the build and tests what is built. A `core.wbx` older than a
  source or a patch then fails the `build:fresh` leg.
- `-d` names the folder that holds the game's files (default
  `tests/roms-local`; `POP_DIR` also sets it).

It needs miniBox built. It builds the native reference and the guest, then
proves that the sandboxed core plays exactly as the native reference does
(picture, sound, every step's length and lag, the clock, every memory domain),
that a second sandboxed run is the same, that a savestate before every step
loses nothing, and that a new host can finish a run from a state taken half
way. It then checks the pictures, a known route, the step rates, the property
table, pokes and freezes, the settings, the four releases, the commands, the
cheats, the file slots, the refusals and that the package is the same twice.

It prints one line per leg (PASS, FAIL or SKIP) and ends with
`<n> ok, <n> failed, <n> skipped`. The exit status is 0 only when nothing
failed. Its work files and pictures are in `build/gate/`.

The game's files are the user's and are never in the repository. Put each
release's files (its DAT files and PRINCE.EXE) in `tests/roms-local/pop10`,
`pop11`, `pop13` and `pop14`. That folder is gitignored.

- Without 1.0's files only the build, the declarations (`wire:config==driver`)
  and the refusal of a project with no data run. Everything else is one
  `game SKIP` line, the package leg included. This is what CI runs: a public
  runner does not have the game.
- Without 1.4's files the `route14` legs are skipped.
- Unless 1.1's, 1.3's and 1.4's files are all there, the `releases` legs are
  skipped.

### The frontend gate

```
./waterbox/tests/run-frontend.sh --chimera-root <chimera>
```

Usage: `./waterbox/tests/run-frontend.sh [--chimera-root <path>] [-m <miniBox dir>] [-d <PoP 1.0 dir>]`

It proves the package as Chimera loads it: `chimera-run` (the engine with no
frontend) and Chimera itself, headless under Mono, play a project and end
with the native reference's memory and picture; the core's refusal reaches
the frontend; the package's key bindings become the defaults. CI does not run
it.

It needs:

- 1.0's files (`-d`, default `tests/roms-local/pop10`; `POP10_DIR` also sets
  it). Without them it prints one line and exits 0 with nothing run.
- `build/native/run-native`, from `native.mk`. Without it the script stops.
- A built Chimera checkout: `--chimera-root`, else `../chimera` beside this
  repository, else `~/chimera`. Without `<chimera>/build/dll/chimera-run` the
  `engine` legs are skipped; without `<chimera>/build/Chimera.exe` the
  `frontend` legs are.
- `mono`, and `Xvfb` when `DISPLAY` is not set: the script then starts a
  private one. Without Xvfb the `frontend` legs are skipped.

It builds the package itself and writes only into `build/frontend/`, never
into the Chimera checkout.

### Chimera's contract tests

CI runs Chimera's own tests against the package it just built. They need
Chimera built first, with the workflow's commands:

```
cd <chimera>
meson setup build/meson-linux --prefix "$PWD/build" --libdir dll
meson compile -C build/meson-linux
meson install -C build/meson-linux
dotnet build source/gui/Chimera.sln -c Release /nodeReuse:false -p:UseSharedCompilation=false
```

CI runs these before it builds miniBox. The core's own build reads only
miniBox's build directory, so the order between the two does not matter.

Then, with the package in `<chimera>/build/Cores`:

```
cd <chimera>
CHIMERA_CORES_DIR="$PWD/build/Cores" dotnet test source/gui/Chimera.Tests.Client.Common/Chimera.Tests.Client.Common.csproj \
  -c Release --nologo \
  --filter "FullyQualifiedName~InstalledCorePackagesTests|FullyQualifiedName~MnemonicUniquenessTests"
```

They prove the package is readable, is built for an ABI this frontend runs,
makes a working factory, binds only buttons its controller declares, and
stamps a version. They need none of the game's files.

## Files the core needs at run time

The package carries none of the game's data, and the core cannot read
SDLPoP's own extracted data folders. The user provides the original DOS
release's files, and the project brings them as firmware. The release is the
`version` setting, picked in the new-project wizard's System box: 1.0 (the
default), 1.1, 1.3 or 1.4. `waterbox/waterbox.config` declares each file with
the release's size and SHA-1.

For every release:

- `PRINCE.EXE`. It is never run. It tells 1.0 from 1.1 and 1.3 from 1.4,
  whose data files are the same.
- `PRINCE.DAT`, `KID.DAT`, `LEVELS.DAT`, `TITLE.DAT`, `PV.DAT`,
  `VDUNGEON.DAT`, `VPALACE.DAT`, `GUARD.DAT`, `GUARD1.DAT`, `GUARD2.DAT`,
  `FAT.DAT`, `SKEL.DAT`, `VIZIER.DAT`, `SHADOW.DAT`, `IBM_SND1.DAT`,
  `IBM_SND2.DAT`.

Only with the Sound card setting at `digital` (the default; the other value
is `pcSpeaker`):

- `DIGISND1.DAT`, `DIGISND2.DAT`, `DIGISND3.DAT`, `MIDISND1.DAT`,
  `MIDISND2.DAT`.

`LEVELS.DAT` is not needed when the project fills the `levels` slot.

Where they come from: the repository says only that they are the user's own
copy of the original release. The package names the four releases
"Prince of Persia 1.0 (1990)", "1.1 (IBM PC)", "1.3 (1992)" and
"1.4 (Collection CD)".

A missing file is refused by name. A file of the user's own (a modified one,
another release's) may take an original's place: the core takes it as it is
and the project pins its hash.

A project may also add, in its file slots (`waterbox/file_slots.json`):

- `levels`: a custom level set, a `LEVELS.DAT` played in place of the
  original levels.
- `savestate`: an SDLPoP quicksave (`QUICKSAVE.SAV`) the run starts from.

## Troubleshooting

- `miniBox not found` from the gate or the package script: they fell back to
  `~/chimera/extern/chimera-common-minibox` and it is not there. Pass `-m` or
  export `MINIBOX_DIR`.
- `miniBox's C guest toolchain is missing: .../build/meson-linux/musl-gcc`:
  miniBox is not built. See "Build miniBox".
- `extern/SDLPoP is not checked out`: run
  `git submodule update --init --recursive`.
- `the series does not apply to the submodule's HEAD at <patch>`: the
  submodule was moved without rebasing the patches.
- `extern/SDLPoP is partly patched`: the tree is neither pristine nor exactly
  what the series leaves. To start again from the submodule's HEAD, as the
  script says:

  ```
  git -C extern/SDLPoP reset --hard && git -C extern/SDLPoP clean -fd && waterbox/apply-patches.sh
  ```

  That discards edits made in the tree. Turn them into a patch first.
- `the guest build failed (build/package-make.log)` from `build-package.sh`:
  the last twenty lines of that log are printed above the message; the
  whole of it is in the file.
- `build:fresh FAIL` in the gate: `-q` was used on a `core.wbx` older than a
  source or a patch. Run the gate without `-q`.
- `nothing built to test`: `-q` was used before anything was built.
- A gate leg fails in the build: the logs are `build/gate/native-make.log`
  and `build/gate/guest-make.log`.
- make decides by file times. A file restored from a backup keeps the
  backup's older time, and make then keeps the old objects. Touch a restored
  file (`docs/PLAN.md`, "Negative controls").
- `packaging is not deterministic`: two archives of the same staging folder
  differed. The package's SHA-1 is the core's identity, so the script stops.
