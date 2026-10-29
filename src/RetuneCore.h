#pragma once

// RetuneCore: the note-transposing part of Retune, with no CLAP in it. One UMP message in, zero
// to two out. The UMP itself and the latch table come from uw-core's uw::ump (NoteRemap, the same
// shift for every key); this file holds only Retune's own rules: the parameters and the shift
// (docs/design.md, "Behaviour").

#include <cstddef>
#include <cstdint>

#include "uw/ump/MidiMessage.h"
#include "uw/ump/NoteRemap.h"

namespace retune {

// Parameter ranges (docs/design.md, "Parameters").
inline constexpr int kMinDivisions = 1;
inline constexpr int kMaxDivisions = 96;
inline constexpr int kMaxOctaves   = 4;

struct Params {
    int divisions = 12;   // N: equal steps per octave
    int octaves   = 0;
    int steps     = 0;    // steps of 1/N octave; clamped to ±N when used
};

// Params with every field clamped to its range (Steps to ±N, after N is clamped).
Params clamped(const Params& p) noexcept;

// The shift in 1/512 semitone (Pitch 7.9 units), rounded to nearest: 512 × (12 × Octaves +
// 12 × Steps / N).
int shift79(const Params& p) noexcept;

// The shift in whole semitones for the MIDI 1.0 path: 12 × Octaves + 12 × Steps / N, rounded
// to nearest, halves away from zero.
int shiftSemitones(const Params& p) noexcept;

// Most messages one process() call can write: a retriggered note whose output key moved
// writes a Note Off for the old key before the new Note On.
inline constexpr std::size_t kMaxOut = uw::ump::NoteRemap::kMaxOut;

class RetuneCore {
public:
    RetuneCore() noexcept { setParams(Params{}); }

    // Takes effect for notes that start after the call; sounding notes keep their latched key.
    void setParams(const Params& p) noexcept;
    const Params& params() const noexcept { return params_; }

    // Transpose one message. MIDI 1.0 notes (MT 0x2) move by shiftSemitones(); MIDI 2.0 notes
    // (MT 0x4) move by shift79() and leave with a Pitch 7.9 attribute. Note Off, poly pressure
    // and MIDI 2.0 per-note messages follow their Note On's latched key, and a Pitch 7.25 value
    // moves by the Note On's full shift, fraction included. A note whose result
    // leaves 0..127 is dropped, with everything that follows it. All Sound Off (CC 120) and All
    // Notes Off (CC 123) clear that channel's latches; everything else passes through
    // unchanged. Writes at most kMaxOut messages to `out`; returns the count.
    std::size_t process(const uw::MidiMessage& in, uw::MidiMessage* out) noexcept {
        return remap_.process(in, out);
    }

    // Forget every sounding note (host reset).
    void reset() noexcept { remap_.reset(); }

private:
    Params             params_;
    uw::ump::NoteRemap remap_;
};

} // namespace retune
