# Building Retune

Retune builds the same way on Linux and macOS: CMake + Ninja, two git submodules, C++20.
The build makes the core library `retune_core`, the plugin `Retune.clap`, and two test programs:
`retune-core-tests` (the core) and `retune-clap-tests` (a headless host that loads the built `.clap`).

## What you need

| | Linux | macOS |
|---|---|---|
| Compiler | GCC 11+ or Clang 14+ (C++20) | Xcode Command Line Tools (Apple Clang) |
| CMake | 3.21+ | 3.21+ |
| Ninja | yes | yes |
| Install | `sudo apt install build-essential cmake ninja-build git` | `xcode-select --install`, then `brew install cmake ninja` |

CMake 3.21 is uw-core's minimum (Retune's own `CMakeLists.txt` asks for less).
No JACK, ALSA, CoreAudio or CoreMIDI is needed: Retune builds uw-core with
`UW_CORE_UMP_ONLY`, which makes only `uw::ump`.

## Get the source

```bash
git clone https://github.com/erickampman/Retune.git
cd Retune
git submodule update --init
```

If you already cloned without the submodules, `external/clap` and `external/uw-core` are empty
directories and CMake fails at `add_subdirectory`. Run `git submodule update --init` to fix it.
`--recursive` isn't needed: uw-core's own submodules are only used outside the UMP-only build.

## Build and test

The same on both platforms:

```bash
cmake -S . -B build -G Ninja
ninja -C build
ctest --test-dir build --output-on-failure
```

The output goes in `build/` (ignored by git). The plugin is `build/Retune.clap`: a shared object on
Linux, a bundle directory on macOS.

Release build: add `-DCMAKE_BUILD_TYPE=Release` to the first command.

### Installing the plugin for hosts

```bash
ninja -C build install-clap
```

This symlinks the built plugin into the user CLAP folder, so a rebuild is picked up without
reinstalling:

| Linux | macOS |
|---|---|
| `~/.clap/Retune.clap` | `~/Library/Audio/Plug-Ins/CLAP/Retune.clap` |

An ordinary build never writes outside `build/`; only this target does.

### Dialect log

`-DRETUNE_DIALECT_LOG=ON` builds a diagnostic plugin that logs the host's note dialects and every
incoming note event. Leave it off for normal use.

### macOS: universal binaries

To build for both Apple Silicon and Intel:

```bash
cmake -S . -B build -G Ninja -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
```

## Building against a local uw-core

To try a uw-core change that isn't pushed and pinned yet, point the build at another clone:

```bash
cmake -S . -B build -G Ninja -DRETUNE_UW_CORE_DIR=$HOME/Dev-Common/uw-core
```

When the change is ready, push uw-core first, then move Retune's submodule pin
(uw-core's README, "Pushing: uw-core first").

## Checked so far

- **Linux:** GCC 15.2, Ninja: core and plugin build, both tests pass (2026-10-21).
