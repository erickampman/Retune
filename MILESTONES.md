# Retune milestones

## Tests
- all: cmake -S . -B build -G Ninja && ninja -C build && ctest --test-dir build --output-on-failure
- validator: clap-validator validate build/Retune.clap (35 pass, 9 skipped: no presets or audio ports)
- install: ninja -C build install-clap install-vst3 install-auv2 (CLAP and VST3 symlinked, the AU copied)
- au: auval -v aumi Rtun nLKL
- vst3: Steinberg's validator (built from external/vst3sdk) on build/Retune.vst3

## M0: Plan
- [x] docs/design.md: fixed EDO transpose, MIDI 2.0 base case, Pitch 7.9 out, latching, clap-wrapper survey
- [x] Parameters: Divisions (5–96; 1–96 until 2026-11-03), Octaves (±4), Steps (clamped to ±N)
- [x] Repo erickampman/Retune, MIT
- [x] UMP code from uw-core's uw::ump, not Retune's own
- [x] CLAP note dialect: add it (M3), since Bitwig sends plugins no MIDI 2.0 (2026-10-21)

## M1: Core library
- [x] uw-core: `notePitch79`, `transposeNote79`, `transposePitch725` in uw::ump, with tests (uw-core 89dd48e, pinned)
- [x] CMake skeleton, external/clap and external/uw-core (UMP-only) submodules, ctest
- [x] RetuneCore: shift maths (12 × Octaves + 12 × Steps / N), Pitch 7.9 read/write
- [x] MIDI 1.0 path (rounded) and MIDI 2.0 path (attribute type 3)
- [x] Latch table: Note Off, poly pressure and MIDI 2.0 per-note messages use the latched key
- [x] Out-of-range drop, pass-through of everything else, All Notes Off / reset clear the latch
- [x] Retrigger ends the old output key; unlatched Note Off / per-note messages follow the current shift
- [x] A host's Pitch 7.25 on a shifted note moves by the note's full shift, fraction included
- [x] Table-driven tests: both protocols, with and without an incoming pitch attribute, held-note parameter change, several N values
- [x] The latch and the CLAP note dialect moved to uw-core (`NoteRemap`, `ClapNoteRemap`, uw-core a33f7ce) to share with other note effects; same tests pass (2026-10-29)

## M2: Raw CLAP plugin
- [x] clap_entry, note-effect feature, note ports in + out (MIDI | MIDI2, prefer MIDI2)
- [x] Three parameters, state save/load, clap-validator clean
- [x] Headless host test: MIDI 1.0 and MIDI 2.0 in, output events checked
- [x] Dialect log (`-DRETUNE_DIALECT_LOG=ON`): host `supported_dialects()` and incoming event counts, to ~/Library/Logs/Retune-dialects.log
- [x] Dialect log in Bitwig 6.1.3: host dialects CLAP | MIDI | MIDI_MPE (no MIDI2); Retune got MIDI 1.0 only
- [ ] Dialect log in REAPER
- [ ] Heard through StageHand → Retune → StruckSynth (needs StageHand's note-effect hosting, below)

## M3: CLAP note dialect (keeps the fraction in Bitwig)
- [x] Note ports also advertise `CLAP` (preference stays MIDI2)
- [x] CLAP Note On: nearest whole key (as MIDI 1.0) + `CLAP_NOTE_EXPRESSION_TUNING` (same note_id, right after) with the signed fraction
- [x] Latch by note_id too; Note Off / choke / other expressions follow the latched key
- [x] A host's own tuning expression on a shifted note gets the fraction added
- [x] Headless host test for CLAP note events; clap-validator's CLAP note tests run
- [x] Heard in Bitwig with a synth that honours tuning expressions, at 36-EDO (2026-10-21: changing Steps does the right thing)

## M4: Other formats (clap-wrapper)
- [x] AUv2 (`aumi`, `Rtun`/`nLKL`) and VST3 via clap-wrapper v0.16.0: MIDI 1.0 (rounded) behaviour; auval and Steinberg's validator pass (2026-10-21)
- [x] Under clap-wrapper the ports prefer MIDI 1.0, and both note ports have id 0 (the AUv2 wrapper routes output by id)
- [ ] AUv3: needs an app extension inside a containing app; deferred
- [ ] VST3 built and checked on Linux
- [ ] Checked in Logic (MIDI FX slot), REAPER and Bitwig

## M5: Later
- [ ] Wrapper patches to keep the fraction (AU MIDI 2.0 event lists, VST3 `noteOn.tuning`)
- [x] GUI: Divisions, Octaves, Steps, resulting cents; all three as rings of one dial (2026-11-03, design.md "Editor")
- [ ] Editor seen in StageHand, Bitwig and an AU host, and on Linux
- [x] Dry: the original notes out too, in every dialect, latched; a checkbox in the editor (2026-11-03)
- [ ] Dry: a dry note and another key's shifted note on one key (uw-core's NoteRemap owner table)

## Outside Retune (StageHand)
- [x] `outEventsTryPush` keeps plugin note output (MIDI 1.0 and 2.0 as UMP)
- [x] Per-row MIDI-effect chain before the instrument
- [x] Rack format + UI for a row's MIDI effects (the MIDI FX menu; StageHand's docs/midi-effects.md)
- [x] Fake note-effect plugin in tests, then Retune → StruckSynth end to end (StageHand's M3)
