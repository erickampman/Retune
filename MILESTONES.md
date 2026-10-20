# Retune milestones

## Tests
- none yet (phase 1 adds: ninja -C build && ctest --test-dir build --output-on-failure)

## M0: Plan
- [x] PLAN.md: fixed EDO transpose, MIDI 2.0 base case, Pitch 7.9 out, latching, clap-wrapper survey
- [x] Parameters: Divisions (1–96), Octaves (±4), Steps (clamped to ±N)
- [x] Private repo erickampman/Retune
- [x] UMP code from uw-core's uw::ump, not Retune's own
- [ ] CLAP note dialect: decide after the dialect log (M2) shows what Bitwig sends

## M1: Core library
- [ ] uw-core: fractional Pitch 7.9 transpose in uw::ump, with tests (pushed before the pin)
- [ ] CMake skeleton, external/clap and external/uw-core (UMP-only) submodules, ctest
- [ ] RetuneCore: shift maths (12 × Octaves + 12 × Steps / N), Pitch 7.9 read/write
- [ ] MIDI 1.0 path (rounded) and MIDI 2.0 path (attribute type 3)
- [ ] Latch table: Note Off, poly pressure and MIDI 2.0 per-note messages use the latched key
- [ ] Out-of-range drop, pass-through of everything else, All Notes Off / reset clear the latch
- [ ] Table-driven tests: both protocols, with and without an incoming pitch attribute, held-note parameter change, several N values

## M2: Raw CLAP plugin
- [ ] clap_entry, note-effect feature, note ports in + out (MIDI | MIDI2, prefer MIDI2)
- [ ] Three parameters, state save/load, clap-validator clean
- [ ] Headless host test: MIDI 1.0 and MIDI 2.0 in, output events checked
- [ ] Debug dialect log: host `supported_dialects()` and incoming event types (Bitwig, REAPER)
- [ ] Heard through StageHand → Retune → StruckSynth

## M3: Other formats (clap-wrapper)
- [ ] AUv2 / AUv3 (`aumi`), VST3: MIDI 1.0 (rounded) behaviour
- [ ] Checked in Logic (MIDI FX slot), REAPER and Bitwig

## M4: Later
- [ ] Wrapper patches to keep the fraction (AU MIDI 2.0 event lists, VST3 `noteOn.tuning`)
- [ ] GUI: Divisions, Octaves, Steps, resulting cents (ask first: UI-visible)

## Outside Retune (StageHand)
- [ ] `outEventsTryPush` keeps plugin note output (MIDI 1.0 and 2.0 as UMP)
- [ ] Per-row MIDI-effect chain before the instrument
- [ ] Rack format + UI for a row's MIDI effects (ask first: UI-visible)
- [ ] Fake note-effect plugin in tests, then Retune → StruckSynth end to end
