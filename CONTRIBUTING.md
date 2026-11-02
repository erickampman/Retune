# Contributing to Retune

Bug reports and pull requests are welcome. Building: [BUILDING.md](BUILDING.md); how it works and
why: [docs/design.md](docs/design.md). This page is the rules the code depends on.

## Before each commit

```sh
ctest --test-dir build --output-on-failure
clap-validator validate build/Retune.clap
```

`retune-core` tests the shift itself; `retune-clap` loads the built `.clap` and plays it headless,
sending MIDI 1.0, MIDI 2.0 and CLAP notes, so it needs no MIDI hardware. On macOS, if you touched the
AUv2 build, also run `auval -v aumi Rtun nLKL`.

## Rules

- **Parameters: append, never insert.** The CLAP parameter id is the parameter's index, and hosts
  save sessions and automation by id: a new parameter goes at the end, and none is reordered or
  removed.
- **No stuck notes.** Every sounding note remembers where it was sent, so its note-off, poly
  pressure and per-note messages follow it there even if the shift changed while it was held.
  Anything that sends a note keeps that promise.
- **Real time.** No allocation, locks or threads on the audio thread.
- **UMP code comes from uw-core** (`uw::ump`, `uw::clap`); Retune writes no UMP parsing of its own.

## Changing uw-core

A change Retune needs from uw-core is committed and pushed there first, then the pin
(`external/uw-core`) moves here. `git config push.recurseSubmodules check` makes git refuse a pin
uw-core's remote doesn't have yet.

## Layout

```
src/RetuneCore.*      the shift, over uw::ump: no CLAP in it
src/plugin.cpp        the CLAP plugin: entry, note ports and dialects, params, state
test/                 RetuneCoreTests (the shift) and RetuneClapTests (the built .clap, headless)
tools/fetch_deps.sh   fetches the submodules
external/clap         CLAP headers (submodule)
external/uw-core      uw-core (submodule), built UMP-only
external/clap-wrapper free-audio's clap-wrapper (submodule): the VST3 and AUv2 builds
external/vst3sdk      Steinberg VST3 SDK (submodule; only the parts the build uses)
```

## Third-party code and licenses

Retune is MIT licensed, and contributions are taken under the same license: a pull request is
offered as MIT.

- **What ships.** [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) lists every third-party
  component in the builds, with its version, license and full license text, and every binary
  package includes it. A change that adds a component, or moves one to a new version, updates its
  entry in the same commit.
- **Upstream code stays as it came.** Submodules aren't edited in place: a fix goes upstream, to
  Unlikelyware's DPF or pugl fork, or into a patch file that `tools/fetch_deps.sh` applies.
  Copied-in files keep their copyright and license headers.
- **A new dependency** must allow MIT-licensed source and binary distribution with nothing more
  than keeping its notice: MIT, BSD, ISC, zlib, Apache-2.0 (with its NOTICE file, if it has one) or
  public domain, and the SIL Open Font License for fonts. No GPL or LGPL code.
- **Trademarks.** VST® is a registered trademark of Steinberg: the first "VST" in any document
  carries the ®, and a document that names VST ends with Steinberg's attribution line. Showing the
  VST Compatible Logo brings in the rest of [Steinberg's VST Usage Guidelines](https://github.com/steinbergmedia/vst3sdk/blob/master/VST3_Usage_Guidelines.pdf). Name
  hosts and other products plainly, to say what works with what, never so it reads as an
  endorsement.

VST is a registered trademark of Steinberg Media Technologies GmbH.
