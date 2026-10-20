#include "RetuneCore.h"

#include <algorithm>

#include "uw/ump/Ump.h"

namespace retune {

namespace ump = uw::ump;

namespace {

// num / den rounded to nearest, halves away from zero (den > 0).
int roundDiv(long num, long den) noexcept {
    return static_cast<int>(num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den));
}

// Messages whose note number names a sounding note, apart from Note On/Off.
bool isPerNote(const uw::MidiMessage& m) noexcept {
    const ump::Status s = ump::status(m);
    if (s == ump::Status::PolyPressure) return true;
    return ump::messageType(m) == ump::MessageType::Midi2ChannelVoice &&
           (s == ump::Status::RegisteredPerNoteController ||
            s == ump::Status::AssignablePerNoteController ||
            s == ump::Status::PerNotePitchBend || s == ump::Status::PerNoteManagement);
}

// A Pitch 7.25: registered per-note controller 3, an absolute pitch.
bool isPitch725(const uw::MidiMessage& m) noexcept {
    return ump::messageType(m) == ump::MessageType::Midi2ChannelVoice &&
           ump::status(m) == ump::Status::RegisteredPerNoteController && (m.words[0] & 0xFF) == 3;
}

// All Sound Off (CC 120) or All Notes Off (CC 123); the controller index sits in the same
// bits in MT 0x2 and MT 0x4.
bool isAllNotesOff(const uw::MidiMessage& m) noexcept {
    if (ump::status(m) != ump::Status::ControlChange) return false;
    const auto index = (m.words[0] >> 8) & 0x7F;
    return index == 120 || index == 123;
}

} // namespace

Params clamped(const Params& p) noexcept {
    Params c;
    c.divisions = std::clamp(p.divisions, kMinDivisions, kMaxDivisions);
    c.octaves   = std::clamp(p.octaves, -kMaxOctaves, kMaxOctaves);
    c.steps     = std::clamp(p.steps, -c.divisions, c.divisions);
    return c;
}

int shift79(const Params& p) noexcept {
    const Params c = clamped(p);
    return roundDiv(512L * 12 * (static_cast<long>(c.octaves) * c.divisions + c.steps), c.divisions);
}

int shiftSemitones(const Params& p) noexcept {
    const Params c = clamped(p);
    return roundDiv(12L * (static_cast<long>(c.octaves) * c.divisions + c.steps), c.divisions);
}

void RetuneCore::setParams(const Params& p) noexcept {
    params_         = clamped(p);
    shift79_        = shift79(params_);
    shiftSemitones_ = shiftSemitones(params_);
}

void RetuneCore::reset() noexcept {
    for (auto& group : latch_)
        for (auto& channel : group)
            for (auto& e : channel) e = Entry{};
}

RetuneCore::Entry& RetuneCore::entry(const uw::MidiMessage& m) noexcept {
    return latch_[ump::group(m)][ump::channel(m)][ump::noteNumber(m)];
}

bool RetuneCore::transposeNoteOn(uw::MidiMessage& m) const noexcept {
    if (ump::messageType(m) == ump::MessageType::Midi2ChannelVoice)
        return ump::transposeNote79(m, shift79_);
    return ump::transposeNote(m, shiftSemitones_);
}

int RetuneCore::unlatchedKeyShift(const uw::MidiMessage& m) const noexcept {
    if (ump::messageType(m) != ump::MessageType::Midi2ChannelVoice) return shiftSemitones_;
    // MIDI 2.0: the Note On's key is the integer part of its shifted pitch (floor).
    return shift79_ >= 0 ? shift79_ / 512 : -((-shift79_ + 511) / 512);
}

int RetuneCore::pitchShift(const uw::MidiMessage& m) const noexcept {
    return ump::messageType(m) == ump::MessageType::Midi2ChannelVoice ? shift79_
                                                                       : shiftSemitones_ * 512;
}

bool RetuneCore::moveToKey(uw::MidiMessage& m, int keyShift, int shift79) const noexcept {
    if (!ump::transposeNote(m, keyShift)) return false;
    // transposeNote moved a Pitch 7.25 value by whole semitones with its key; add the fraction
    // so the value moves by the note's full shift.
    if (isPitch725(m)) return ump::transposePitch725(m, shift79 - keyShift * 512);
    return true;
}

std::size_t RetuneCore::process(const uw::MidiMessage& in, uw::MidiMessage* out) noexcept {
    if (!ump::isChannelVoice(in)) {
        out[0] = in;
        return 1;
    }

    if (ump::isNoteOn(in)) {
        Entry&      e = entry(in);
        std::size_t n = 0;
        uw::MidiMessage m = in;
        const bool inRange = transposeNoteOn(m);
        if (e.state == Latch::Sounding && (!inRange || e.outKey != ump::noteNumber(m))) {
            // Retriggered while sounding, and the new note won't land on the old key: end the
            // old one first so it can't hang.
            uw::MidiMessage off = in;
            off.words[0] = (off.words[0] & 0xFF0F00FFu) |
                           (static_cast<std::uint32_t>(ump::Status::NoteOff) << 20) |
                           (static_cast<std::uint32_t>(e.outKey) << 8);
            off.words[0] &= ~0xFFu;       // MIDI 1.0: velocity 0; MIDI 2.0: no attribute
            off.words[1] = 0;
            out[n++] = off;
        }
        if (!inRange) {
            e = {Latch::Dropped, 0, 0};
            return n;
        }
        e = {Latch::Sounding, ump::noteNumber(m), static_cast<std::int16_t>(pitchShift(in))};
        out[n++] = m;
        return n;
    }

    if (ump::isNoteOff(in) || isPerNote(in)) {
        Entry& e = entry(in);
        uw::MidiMessage m = in;
        bool keep = true;
        switch (e.state) {
            case Latch::Sounding:
                keep = moveToKey(m, e.outKey - ump::noteNumber(in), e.shift79);
                break;
            case Latch::Dropped:
                keep = false;
                break;
            case Latch::None:
                // No Note On seen (it came before a reset, or before Retune started): guess the
                // key the current shift would have given it.
                keep = moveToKey(m, unlatchedKeyShift(in), pitchShift(in));
                break;
        }
        if (ump::isNoteOff(in)) e = Entry{};
        if (!keep) return 0;
        out[0] = m;
        return 1;
    }

    if (isAllNotesOff(in))
        for (auto& e : latch_[ump::group(in)][ump::channel(in)]) e = Entry{};
    out[0] = in;
    return 1;
}

} // namespace retune
