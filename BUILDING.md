# Building Retune

Retune builds the same way on Linux and macOS: CMake + Ninja, git submodules, C++20.
The build makes the core library `retune_core`, the plugin `Retune.clap`, and two test programs:
`retune-core-tests` (the core) and `retune-clap-tests` (a headless host that loads the built `.clap`).
It also wraps the same plugin with clap-wrapper: `Retune.vst3` (Linux and macOS) and
`Retune.component`, an AUv2 MIDI effect (macOS only).

## What you need

**macOS:** macOS 26 or later on Apple Silicon (arm64). Intel Macs aren't supported.

- The Xcode Command Line Tools: `xcode-select --install`. They bring clang, the macOS SDK, git and
  Python 3.
- [Homebrew](https://brew.sh), then `brew install cmake ninja`.
- Full Xcode isn't needed.

**Linux:** a C++20 compiler (GCC 11+ or Clang 14+), CMake 3.21+, Ninja, Python 3 and git. On Debian
or Ubuntu this installs them and every library the build needs:

```bash
sudo apt install build-essential cmake ninja-build pkg-config python3 git \
  libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxinerama-dev \
  libxcb1-dev libx11-xcb-dev libxcb-util-dev libxkbcommon-dev libxkbcommon-x11-dev \
  libfontconfig1-dev libasound2-dev libjack-jackd2-dev \
  libxfixes-dev libxi-dev libxss-dev libxtst-dev libwayland-dev wayland-protocols \
  libegl1-mesa-dev libgles2-mesa-dev libdrm-dev libgbm-dev libdecor-0-dev libudev-dev \
  libdbus-1-dev libibus-1.0-dev
```

It's one list for every Unlikelyware project (the same one CI installs), so it covers more than
Retune needs. On other distros, install the same libraries under their own names.

Retune itself needs none of those libraries: it builds uw-core with `UW_CORE_UMP_ONLY`, which makes
only `uw::ump` (no JACK, ALSA, CoreAudio or CoreMIDI).

## Get the source

```bash
git clone https://github.com/erickampman/Retune.git
cd Retune
tools/fetch_deps.sh
```

`tools/fetch_deps.sh` checks out the git submodules at their pinned commits. Run it again after a
`git pull` that moves, adds or re-points a submodule: a plain pull leaves them where they were.

Of the VST3 SDK's nested submodules it takes only the four the build uses; `--recursive` would also
download VSTGUI, the SDK tutorials and its docs. If the folders under `external/` are empty, CMake
fails at `add_subdirectory`, and clap-wrapper stops with "There is no VST3 SDK at
…/external/vst3sdk" when the SDK's own submodules are: run `tools/fetch_deps.sh` to fix either.

## Build and test

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The same three commands on both platforms. Everything goes in `build/` (ignored by git).

The plugin is `build/Retune.clap`: a shared object on Linux, a bundle directory on macOS.

## Installing the plugin for hosts

A plain `cmake --build build` writes nothing outside `build/`. These targets put the plugin where
hosts look for it:

```bash
cmake --build build --target install-clap
cmake --build build --target install-vst3
cmake --build build --target install-auv2     # macOS
```

| | Linux | macOS |
|---|---|---|
| CLAP | `~/.clap` | `~/Library/Audio/Plug-Ins/CLAP` |
| VST3 | `~/.vst3` | `~/Library/Audio/Plug-Ins/VST3` |
| AUv2 | — | `~/Library/Audio/Plug-Ins/Components` |

`install-clap` and `install-vst3` symlink the plugin in `build/`, so a host picks up a rebuild
after a rescan. The link points into this clone: delete the clone and the host loses the plugin,
and running the target in another build folder points the host there instead. `install-auv2`
copies the component (AU hosts don't reliably follow symlinks), so run it again after a rebuild.

To keep the plugin without the clone, copy it instead (the `rm` clears a link the targets left):

```bash
# Linux
mkdir -p ~/.clap ~/.vst3
rm -rf ~/.clap/Retune.clap ~/.vst3/Retune.vst3
cp build/Retune.clap ~/.clap/
cp -R build/Release/Retune.vst3 ~/.vst3/
```

```bash
# macOS
mkdir -p ~/Library/Audio/Plug-Ins/CLAP ~/Library/Audio/Plug-Ins/VST3
rm -rf ~/Library/Audio/Plug-Ins/CLAP/Retune.clap ~/Library/Audio/Plug-Ins/VST3/Retune.vst3
cp -R build/Retune.clap ~/Library/Audio/Plug-Ins/CLAP/
cp -R build/Retune.vst3 ~/Library/Audio/Plug-Ins/VST3/
```

For every user instead, the same folders without `~` (`/usr/lib/clap` and `/usr/lib/vst3` on Linux,
`/Library/Audio/Plug-Ins/...` on macOS), with `sudo`. Then rescan in the host. A host that is
already running keeps the plugin it loaded first: quit and restart it after a rebuild. A plugin you
built yourself has no quarantine flag, so macOS loads it without the steps a downloaded one needs.


## Options

Pass these to the first command (`cmake -B build ... -DNAME=VALUE`).

| Option | Default | Effect |
|---|---|---|
| `RETUNE_BUILD_VST3` | `ON` | Build `Retune.vst3` through clap-wrapper (with the vendored `external/vst3sdk`). |
| `RETUNE_BUILD_AUV2` | `ON` on macOS, `OFF` elsewhere | Build `Retune.component`, an AUv2 MIDI effect (`aumi Rtun nLKL`), through clap-wrapper. **macOS only**. The first configure downloads Apple's AudioUnitSDK; `-DAUDIOUNIT_SDK_ROOT=...` uses a local copy. |
| `RETUNE_DIALECT_LOG` | `OFF` | A diagnostic plugin that logs the host's note dialects and every incoming note event. Leave it off for normal use. |
| `RETUNE_UW_CORE_DIR` | `external/uw-core` | Build against another uw-core clone (below). |

The wrapped formats keep only the MIDI 1.0 (rounded) behaviour. Inside clap-wrapper, Retune prefers
the MIDI 1.0 dialect (docs/design.md, "What clap-wrapper does with a note effect").

## Checking a build

```bash
clap-validator validate build/Retune.clap   # 35 passed, 0 failed, 0 warnings, 9 skipped
auval -v aumi Rtun nLKL                     # AU VALIDATION SUCCEEDED (macOS)
```

[clap-validator](https://github.com/free-audio/clap-validator) is free-audio's CLAP checker.

## Building against a local uw-core

To try a uw-core change that isn't pushed and pinned yet, point the build at another clone:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DRETUNE_UW_CORE_DIR=$HOME/Dev-Common/uw-core
```

When the change is ready, push uw-core first, then move Retune's submodule pin
(uw-core's README, "Pushing: uw-core first").

## Checked so far

- **Linux:** GCC 15.2, Ninja (2026-10-21): the CLAP and VST3 builds work and both tests pass;
  `install-clap` and `install-vst3` link into `~/.clap` and `~/.vst3`. Retune runs in StageHand.
  The VST3 isn't checked in a Linux host yet.
- **macOS:** Xcode 26.3 (Apple Clang), Ninja (2026-10-21):
  - the CLAP, VST3 and AUv2 builds have no warnings, and both tests pass;
  - clap-validator: 35 pass, 0 fail;
  - auval: passes;
  - Steinberg's VST3 validator: 47 pass, 0 fail.

  The VST3 SDK's own CMake rejects an Xcode path with spaces. That only matters when you build its
  `validator` yourself: point `DEVELOPER_DIR` at a symlink without spaces.

## How this differs from the other projects

Every Unlikelyware project builds the same way: `git clone`, `tools/fetch_deps.sh`, the three
commands under "Build and test", and for a plugin `cmake --build build --target install-clap`. The
same package list, the same `build/` folder, the same option names (`<PROJECT>_BUILD_VST3`,
`<PROJECT>_UW_CORE_DIR` and so on). What's different here, and why:

- No UI and no DPF: Retune is a note effect with only parameters, so it has no `dpf` submodule and needs no `patches/` (the synths' clap-wrapper patch is for its AUv2 view).
- The VST3 and AUv2 builds keep only the MIDI 1.0 (rounded) behaviour (above).
- No `_WERROR` option: the five synths have one and their CI turns it on; this project's build has none, so the option can be added when CI arrives.
- No CI yet (ReleaseUMP R1-3, from November 2026).
