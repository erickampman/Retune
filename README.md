# Retune

A MIDI note effect from Unlikelyware that transposes every note by a fixed interval in any
equal division of the octave (EDO): whole octaves plus steps of 1/N octave, for N from 1 to 96.
Put it in front of a synth and a 12-TET keyboard plays in 19-EDO, 31-EDO or 36-EDO steps.

Retune makes no sound of its own. It changes the notes going to the synth after it.

| | |
|---|---|
| Formats | CLAP. VST3 and AUv2 (macOS) build from source; ready-built in a later preview |
| Platforms | Linux x86-64, macOS 26 or later on Apple Silicon. No Windows |
| Version | 0.9.0 (preview) |
| License | MIT |

**Downloads:** ready-built plugins will be on itch.io with the 0.9 preview. This repo has the
source only.

## What makes it different

- **MIDI 2.0 first.** A shifted note keeps its fraction as a Pitch 7.9 attribute on the Note On,
  so a synth that reads it plays the exact EDO step, not the nearest semitone. A receiver that
  ignores the attribute still gets the nearest key below.
- **The CLAP note dialect too.** Hosts such as Bitwig send plugins CLAP notes, not MIDI 2.0.
  There Retune sends the nearest key plus a tuning expression with the fraction, so a synth that
  honours tuning expressions still hears the EDO step.
- **MIDI 1.0 works, rounded** to the nearest semitone. With 12 divisions every shift is a whole
  semitone, so nothing is lost.
- **No stuck notes.** Each sounding note remembers where it went, so changing the shift while
  notes are held moves only new notes. Note Off, poly pressure and MIDI 2.0 per-note messages
  follow the note they belong to.

## Parameters

| Parameter | Range | Default |
|---|---|---|
| Divisions | 1 … 96: N, the steps per octave | 12 |
| Octaves | −4 … +4 | 0 |
| Steps | −N … +N, in steps of 1/N octave | 0 |

The shift is `12 × Octaves + 12 × Steps / N` semitones. Lowering Divisions clamps Steps to the
new range. Notes shifted out of MIDI's 0–127 range are dropped. Everything that isn't a note
(CCs, channel pitch bend, program change, SysEx, clock) passes through unchanged.

## Which hosts keep the fraction

| Host | What Retune gets | Microtonal output |
|---|---|---|
| [StageHand](https://github.com/erickampman/StageHand) | MIDI 2.0 | Yes (Pitch 7.9) |
| Bitwig | CLAP notes | Yes, to a synth that honours tuning expressions |
| Hosts that send MIDI 1.0 only | MIDI 1.0 | Rounded to semitones |
| VST3 and AUv2 builds | MIDI 1.0 | Rounded to semitones |

The Unlikelyware synths ([PlainOldSynth](https://github.com/erickampman/PlainOldSynth),
[StruckSynth](https://github.com/erickampman/StruckSynth) and the rest) play Pitch 7.9 notes.

## Building from source

```bash
git clone https://github.com/erickampman/Retune.git
cd Retune
git submodule update --init
git -C external/vst3sdk submodule update --init base cmake pluginterfaces public.sdk
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
ctest --test-dir build --output-on-failure
ninja -C build install-clap
```

[BUILDING.md](BUILDING.md) has the prerequisites for Linux and macOS, the VST3 and AUv2 options
and where each format installs. [docs/design.md](docs/design.md) explains how it works,
including what clap-wrapper does with a note effect.

## Help and bugs

Questions and bug reports go to [GitHub Issues](https://github.com/erickampman/Retune/issues).

## License

MIT. See [LICENSE](LICENSE).
