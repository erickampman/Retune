# Retune milestones

## Tests
- all: cmake -S . -B build -G Ninja && ninja -C build && ctest --test-dir build --output-on-failure
- validator: clap-validator validate build/Retune.clap (35 pass, 9 skipped: no presets or audio ports)
- install: ninja -C build install-clap (symlinks Retune.clap into the user CLAP folder)

## M0: Plan
- [x] PLAN.md: fixed EDO transpose, MIDI 2.0 base case, Pitch 7.9 out, latching, clap-wrapper survey
- [x] Parameters: Divisions (1–96), Octaves (±4), Steps (clamped to ±N)
- [x] Private repo erickampman/Retune
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
- [ ] Heard in Bitwig with a synth that honours tuning expressions, at 36-EDO

## M4: Other formats (clap-wrapper)
- [ ] AUv2 / AUv3 (`aumi`), VST3: MIDI 1.0 (rounded) behaviour
- [ ] Checked in Logic (MIDI FX slot), REAPER and Bitwig

## M5: Later
- [ ] Wrapper patches to keep the fraction (AU MIDI 2.0 event lists, VST3 `noteOn.tuning`)
- [ ] GUI: Divisions, Octaves, Steps, resulting cents (ask first: UI-visible)

## Outside Retune (StageHand)
- [ ] `outEventsTryPush` keeps plugin note output (MIDI 1.0 and 2.0 as UMP)
- [ ] Per-row MIDI-effect chain before the instrument
- [ ] Rack format + UI for a row's MIDI effects (ask first: UI-visible)
- [ ] Fake note-effect plugin in tests, then Retune → StruckSynth end to end
