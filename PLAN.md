# Retune — plan

A MIDI-in → MIDI-out note effect that transposes every note by a fixed interval in an **equal division of the octave** (N-EDO).
It is CLAP-first, with the same layout as StruckSynth and PlainOldSynth: a hand-written raw CLAP entry
point and the vendored free-audio **clap-wrapper** for the other formats.

**Status (2026-10-20):** phases 1 and 2 are done in code: RetuneCore over uw-core's uw::ump, and the
raw CLAP plugin (clap-validator clean, headless host test). Still to do in phase 2: run the dialect
log in Bitwig and REAPER, and listen through StageHand once it can host note effects.

## Decisions so far

| Question | Answer |
|---|---|
| Kind of shift | Fixed transpose by an interval of an equal division of the octave (EDO): octaves + steps of 1/N octave. Every note moves by the same interval; the keyboard stays 12-TET (no key → N-EDO mapping) |
| MIDI 1.0 output, fractional pitch | Round to the nearest semitone |
| MIDI 2.0 output, fractional pitch | Note On attribute type 3, **Pitch 7.9** |
| Base case | **MIDI 2.0.** MIDI 1.0 is supported (rounded). The CLAP note dialect is supported too: nearest key + a tuning expression (M3) |
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
- **Output format follows the input format.** An event that arrives as MIDI 1.0 leaves as MIDI 1.0.
  An event that arrives as MIDI 2.0 leaves as MIDI 2.0. CLAP note events leave as CLAP note events
  (`src/ClapNotes.h`): the Note On goes to the nearest whole key, as on the MIDI 1.0 path, and a
  `CLAP_NOTE_EXPRESSION_TUNING` right after it carries the signed fraction.
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
- All Notes Off / All Sound Off clear that group and channel's latches; host `reset()` clears them all.
- **Retrigger:** a Note On for a key that is already sounding, whose new output key differs (or is out
  of range), first sends a Note Off for the old output key, so it can't hang.
- **Unlatched** Note Off or per-note message (its Note On came before a reset, or before Retune
  started): it moves to the key the current shift would give a Note On (MIDI 1.0: rounded; MIDI 2.0:
  the integer part).
- A MIDI 2.0 Note On's manufacturer- or profile-specific attribute is replaced by the Pitch 7.9 one.
- **Pitch 7.25** (registered per-note controller 3) is an absolute pitch: on a latched note it moves to
  the latched key and its value moves by the Note On's full shift, fraction included.

## Layout (mirrors StruckSynth)

```
Retune/
  CMakeLists.txt         raw .clap MODULE target, install-clap symlink target, ctest
  external/clap          submodule
  external/uw-core       submodule, UMP-only build (UW_CORE_UMP_ONLY): uw::ump
  external/clap-wrapper  submodule (phase 3)
  src/RetuneCore.{h,cpp} pure C++, no CLAP: shift maths, latch table, over uw::MidiMessage
  src/plugin.cpp         clap_entry, note ports (in + out), params, state
  test/                  core unit tests + a headless host test that dlopens the .clap
```

The core is a plain library over `uw::MidiMessage`, so it can be unit-tested without a host,
and reused later if a CoreMIDI app or another plugin wants it.

**UMP code comes from uw-core** (`~/Dev-Common/uw-core`, `uw::ump`), as in StageHand, KeyboardEDO
and StruckKit (UmpireDAW `docs/decisions.md`, "Shared code"). Retune writes no UMP parsing of its
own: message classification, MIDI 1.0 ↔ 2.0, MIDI 1.0 bytes and the Pitch 7.9 attribute all come
from `uw::ump`. What is about UMP itself goes into uw-core; what is about Retune (parameters, shift,
latch table) stays here.

- **Fractional transpose goes into `uw::ump`.** `transposeNote(m, semitones)` moves a note and its
  Pitch 7.9 attribute by whole semitones only. Add a fractional version to uw-core, e.g.
  `transposeNote79(m, delta)` with `delta` in 1/512 semitone: it adds to the 7.9 pitch (from the
  attribute, or `key << 9`), writes attribute type 3, sets the key to the integer part, and fails
  when the result is out of range. Done as `transposeNote79` (uw-core 89dd48e), with
  `notePitch79` and `transposePitch725`, which moves a Pitch 7.25 value (registered per-note
  controller 3, absolute pitch) by the same units. Commit and push uw-core first, with its own tests, then move
  Retune's pin (uw-core's README, "Pushing: uw-core first").

## Phases

1. **Core library + tests.** uw-core submodule (UMP-only) and the fractional 7.9 transpose in
   `uw::ump` (step 1a, in uw-core). Then transpose rules, Pitch 7.9 maths, latching, out-of-range drop,
   pass-through. Table-driven tests for MIDI 1.0, MIDI 2.0 (with and without an incoming pitch
   attribute), held-note parameter change, several N values, and per-note message remapping.
2. **Raw CLAP plugin.** Features `note-effect` (+ `utility`). One note input port and one note output
   port. Both advertise `MIDI | MIDI2` and prefer `MIDI2` (`CLAP` TBD). Parameter + state save/load,
   and `clap-validator` clean. A headless host test (like `ss-clap`) sends MIDI 1.0 and MIDI 2.0 and
   checks the output events. A debug build logs the host's `supported_dialects()` and the type of every
   incoming note event, to find out what Bitwig, REAPER etc. actually send. Then listen through StageHand → Retune → StruckSynth
   (which already plays 7.9 pitch).
3. **AUv2 / AUv3 / VST3 via clap-wrapper**, MIDI 1.0 (rounded) behaviour only. Done for AUv2 and
   VST3 (M4). Under clap-wrapper the ports prefer MIDI 1.0. Otherwise the AUv2 wrapper would ask an
   AU host for MIDI 2.0, and its MIDI 2.0 → 1.0 output conversion keeps only the key, which rounds
   down. The VST3 wrapper always sends CLAP note events, so ClapNotes gives the rounded key there. Check them in Logic
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

- **CLAP note dialect: added (decided 2026-10-21, done in M3, `src/ClapNotes.h`).** MIDI 2.0 stays the base
  case, but Bitwig sends plugins no MIDI 2.0. How: send the fraction as a `CLAP_NOTE_EXPRESSION_TUNING` (same note_id, right
  after the Note On), add it to any tuning expression the host already sends for that note, and
  remap the key/note_id of other note expressions from the latch table.
  **Dialect log, Bitwig Studio 6.1.3 (2026-10-21):** the host's note dialects are `CLAP | MIDI |
  MIDI_MPE`, with no MIDI2. Retune received only MIDI 1.0 Note On/Off (158 each). So in Bitwig the
  MIDI 2.0 path is never used and the fraction is rounded away; only the CLAP dialect (tuning
  expression) could keep it there.
