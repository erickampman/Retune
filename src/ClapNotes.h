#pragma once

// ClapNotes: Retune's rules for the CLAP note dialect (MILESTONES.md, M3). RetuneCore handles
// MIDI 1.0 and 2.0 as UMP; this handles CLAP_EVENT_NOTE_* and note expressions, which UMP can't
// carry, so Bitwig (no MIDI 2.0 to plugins) can keep the fraction.
//
// A Note On moves to the nearest whole key (shiftSemitones(), the same key as the MIDI 1.0 path)
// and, when the shift has a fraction, is followed at the same time by a
// CLAP_NOTE_EXPRESSION_TUNING with that fraction (same note_id, port, channel and new key). A
// synth that ignores tuning expressions still plays the rounded note.
//
// Each sounding note is latched by channel + key, with its note_id: Note Off, choke and every
// note expression follow the latched key, found by note_id when the event has one. A host's own
// tuning expression on a shifted note gets the note's fraction added. Events with no latched
// note follow the current shift. A Note On whose key leaves 0..127 is dropped, with everything
// that follows it. A wildcard event (key and note_id both -1) passes through, since it matches
// every output key anyway.

#include <clap/clap.h>

#include <cstdint>

#include "RetuneCore.h"

namespace retune {

class ClapNotes {
public:
    ClapNotes() noexcept { reset(); }

    // Takes effect for notes that start after the call; sounding notes keep their latch.
    void setParams(const Params& p) noexcept;

    // One CLAP note event (NOTE_ON, NOTE_OFF, NOTE_CHOKE or NOTE_EXPRESSION) in; the results are
    // pushed to `out`. Anything else is pushed unchanged.
    void process(const clap_event_header_t* in, const clap_output_events_t* out) noexcept;

    // Forget every sounding note (host reset).
    void reset() noexcept;

private:
    enum class Latch : std::uint8_t { None, Sounding, Dropped };
    struct Entry {
        Latch        state      = Latch::None;
        std::uint8_t outKey     = 0;
        std::int16_t fraction79 = 0;    // the tuning the Note On got, in 1/512 semitone
        std::int32_t noteId     = -1;
    };

    void noteOn(const clap_event_note_t& ev, const clap_output_events_t* out) noexcept;
    void noteEnd(const clap_event_note_t& ev, const clap_output_events_t* out) noexcept;
    void expression(const clap_event_note_expression_t& ev, const clap_output_events_t* out) noexcept;

    // Calls f(channel, entry) for each latched note the event's address names; returns how many.
    template <typename F>
    int forEachMatch(int channel, int key, std::int32_t noteId, F&& f) noexcept;

    int   keyShift_   = 0;   // shiftSemitones()
    int   fraction79_ = 0;   // shift79() minus the key shift, in 1/512 semitone
    Entry latch_[16][128];   // [channel][input key]
};

} // namespace retune
