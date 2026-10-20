# Retune — plan

A MIDI-in → MIDI-out note effect that transposes every note by a fixed interval in an **equal division of the octave** (N-EDO).
It is CLAP-first, with the same layout as StruckSynth and PlainOldSynth: a hand-written raw CLAP entry
point and the vendored free-audio **clap-wrapper** for the other formats.

**Status (2026-10-19): on hold, waiting for the author's review.** No code is written yet. The
decisions below were made in the first planning session. The open questions at the end need answers
before phase 1 starts.

## Decisions so far

| Question | Answer |
|---|---|
| Kind of shift | Fixed transpose by an interval of an equal division of the octave (EDO): octaves + steps of 1/N octave. Every note moves by the same interval; the keyboard stays 12-TET (no key → N-EDO mapping) |
| MIDI 1.0 output, fractional pitch | Round to the nearest semitone |
| MIDI 2.0 output, fractional pitch | Note On attribute type 3, **Pitch 7.9** |
| Form | Plugin. CLAP first, then the other formats clap-wrapper supports for MIDI→MIDI |

## What clap-wrapper does with a note effect (checked in PlainOldSynth's copy, v0.16.0)

A CLAP plugin that declares `CLAP_PLUGIN_FEATURE_NOTE_EFFECT` and has a note *output* port:

| Format | Loads as a MIDI effect? | MIDI 1.0 out | MIDI 2.0 out (Pitch 7.9) |
|---|---|---|---|
| **CLAP** (native) | Yes | Yes | **Yes**: we control the port dialects |
| **AUv2** | Yes, type `aumi` (`build-helper.cpp:172`) | Yes | **No**: `CLAP_EVENT_MIDI2` is down-converted to MIDI 1.0 (`wrapasauv2.cpp`, `send`), so the fraction is lost |
| **AUv3** | Yes, type `aumi` (`auv3/build-helper.cpp:251`) | Yes | **No**: the output list is `kMIDIProtocol_1_0` and MIDI2 is down-converted (`auv3/process.mm:821`, `~898`) |
| **VST3** | Partly: mapped to `kInstrumentSynth` (`vst3/categories.cpp:62`), since VST3 has no MIDI-effect category | Note on/off only. `CLAP_EVENT_MIDI` / `MIDI2` output is **dropped** (`vst3/process.cpp:810`) | No. VST3 note events have a `tuning` field (cents), but the wrapper hard-codes it to `0` |

So **every wrapped format can do the MIDI 1.0 (rounded) behaviour** if Retune emits CLAP note
events or MIDI 1.0. **Only native CLAP carries Pitch 7.9 today.** Keeping the fraction in AU or VST3
would take wrapper patches (see the last phase).

## Behaviour

- **Parameters** (appended, never inserted, as in the synths):
  - 0 `Divisions`: N, the number of equal steps per octave. Integer, 1 … 96, default 12.
  - 1 `Octaves`: integer, −4 … +4, default 0.
  - 2 `Steps`: integer, limited to −N … +N, default 0. Steps of 1/N octave; Octaves covers anything
    larger. CLAP parameter ranges are fixed, so the host sees −96 … +96 (max N), and Retune uses
    `clamp(Steps, −N, +N)`. Lowering N therefore clamps the shift at once, and the GUI shows the
    clamped value. The stored value is not rewritten, because a plugin-initiated change would fight
    host automation.
- **Shift** in semitones = `12 × Octaves + 12 × Steps / N` (in cents: `1200 × Octaves + 1200 × Steps / N`).
  With N = 12 every shift is a whole semitone, so MIDI 1.0 output is exact.
- **Output format follows the input format.** An event that arrives as MIDI 1.0 (or a CLAP note event)
  leaves as MIDI 1.0 or a CLAP note event. An event that arrives as MIDI 2.0 leaves as MIDI 2.0.
- **MIDI 1.0 path**: `outKey = inKey + round(shift)`.
- **MIDI 2.0 path**: take the Note On's pitch as 7.9 (from its attribute if it has type 3, otherwise
  `key << 9`), add `round(shift × 512)`, and write it back as attribute type 3. `outKey` is the
  integer part, so a receiver that ignores the attribute still plays the nearest note below.
  Rounding to 1/512 semitone (≈0.2 cent) is fine.
- **Latching**: each sounding note remembers its output key (and 7.9 pitch), keyed by
  group/port + channel + input key (and CLAP note id). Note Off, poly pressure and every MIDI 2.0
  per-note message (per-note CC, per-note pitch bend, per-note management) use the latched key. So
  a parameter change while notes are held cannot leave a stuck note, and only new notes take the new
  value.
- **Out of range** (the result is below 0 or above 127): drop the Note On and its matching Note Off and
  per-note messages.
- **Everything else** (CCs, channel pitch bend, program change, SysEx, clock) passes through
  unchanged, at its original sample offset.
- All Notes Off / All Sound Off / host `reset()` clear the latch table.

## Layout (mirrors StruckSynth)

```
Retune/
  CMakeLists.txt         raw .clap MODULE target, install-clap symlink target, ctest
  external/clap          submodule
  external/clap-wrapper  submodule (phase 3)
  src/RetuneCore.{h,cpp} pure C++, no CLAP: UMP / MIDI 1.0 in → out, latch table
  src/plugin.cpp         clap_entry, note ports (in + out), params, state
  test/                  core unit tests + a headless host test that dlopens the .clap
```

The core is a plain library over a small event struct, so it can be unit-tested without a host,
and reused later if a CoreMIDI app or another plugin wants it.

## Phases

1. **Core library + tests.** Transpose rules, Pitch 7.9 maths, latching, out-of-range drop,
   pass-through. Table-driven tests for MIDI 1.0, MIDI 2.0 (with and without an incoming pitch
   attribute), held-note parameter change, several N values, and per-note message remapping.
2. **Raw CLAP plugin.** Features `note-effect` (+ `utility`). One note input port and one note output
   port. Both advertise `CLAP | MIDI | MIDI2` and prefer `MIDI2`. Parameter + state save/load, and
   `clap-validator` clean. A headless host test (like `ss-clap`) sends MIDI 1.0, MIDI 2.0 and CLAP
   note events and checks the output events. Then listen through StageHand → Retune → StruckSynth
   (which already plays 7.9 pitch).
3. **AUv2 / AUv3 / VST3 via clap-wrapper**, MIDI 1.0 (rounded) behaviour only. Check them in Logic
   (AU MIDI FX slot), REAPER and Bitwig. VST3 loading as an "instrument" is a known limitation.
4. **(Optional) wrapper patches to keep the fraction**:
   - AUv2/AUv3: emit a `kMIDIProtocol_2_0` event list and pass `CLAP_EVENT_MIDI2` through raw when
     the host negotiated MIDI 2.0.
   - VST3: set `noteOn.tuning` from a CLAP tuning note expression or a 7.9 pitch.
   These go in our vendored copy, and possibly upstream.
5. **GUI** (DPF/NanoVG, as in the synths). Three controls: Divisions, Octaves, Steps (plus a read-out of
   the resulting cents). Ask before anything UI-visible.

## StageHand: the main host

StageHand (`~/Dev-MIDI/StageHand`, CLAP hosting in `external/uw-core/src/uw/clap/ClapPlugin.cpp`)
can't use a note effect yet:

- **Plugin output events are dropped.** `ClapPlugin::outEventsTryPush` keeps only param value and
  gesture events. Its `default:` returns without saving anything, so the notes Retune emits go nowhere.
- **No chains.** The v1 engine fans incoming MIDI out to every slot whose `MidiFilter` accepts it,
  and sums the audio (`docs/domain-model.md`, "Engine model"). Nothing feeds one plugin's MIDI output
  into another plugin.
- **The design already has a place for it.** `docs/architecture.md` puts "MIDI effects" on a rack
  row's *input side*, after the sources and filter and before the instrument. `RackWidget` already
  lists note effects under "Other (note effects)", but placing one makes it an ordinary row that
  produces no sound.
- It already speaks both dialects to plugins (`hostNotePortsDialects` = MIDI | MIDI2), and its
  per-slot `MidiFilter::transpose` (whole semitones, pitch-attribute aware, `ump::transposeNote`) is a
  model for Retune's 7.9 maths.

Work in StageHand (its own repo and sessions, after Retune phase 2):

1. `outEventsTryPush`: turn `CLAP_EVENT_NOTE_*`, `MIDI` and `MIDI2` into UMP `MidiEvent`s with their
   sample offsets (MIDI 1.0 as MT `0x2`, MIDI 2.0 as MT `0x4`, the same rule as input).
2. Engine: an ordered list of MIDI-effect plugins per row, run before the instrument in the same
   block. Each one's output event list is the next one's input. They need no audio buffers, or
   0-channel ones.
3. Rack format + UI: save and restore a row's MIDI effects (state included). The row's input side
   gets the "Other (note effects)" menu and an editor button. This is UI-visible, so check with the
   author first.
4. Tests: a fake note-effect plugin in StageHand's tests, then Retune → StruckSynth end to end.

Because StageHand is the main target, the native CLAP build (which keeps Pitch 7.9) matters most. The
AU/VST3 wrappers (phase 3) move down the list.

## Open questions

- **CLAP-dialect output.** For a host that only speaks CLAP note events, should the fraction be sent
  as a `CLAP_NOTE_EXPRESSION_TUNING` after each Note On, or rounded like MIDI 1.0?
