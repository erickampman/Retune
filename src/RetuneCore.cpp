#include "RetuneCore.h"

#include <algorithm>

#include "uw/ump/Ump.h"

namespace retune {

namespace {

// num / den rounded to nearest, halves away from zero (den > 0).
int roundDiv(long num, long den) noexcept {
    return static_cast<int>(num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den));
}

namespace ump = uw::ump;

// The Note Off that ends a note `on` started: same group, channel and key, velocity 0, no attribute.
uw::MidiMessage noteOffFor(const uw::MidiMessage& on) noexcept {
    uw::MidiMessage off = on;
    off.words[0] = (on.words[0] & 0xFF0F'FF00u) | 0x0080'0000u;
    off.words[1] = 0;
    return off;
}

// Messages that belong to one sounding note, beyond its Note On and Off: poly pressure, and MIDI
// 2.0's per-note controllers, per-note pitch bend and per-note management.
bool isPerNote(const uw::MidiMessage& m) noexcept {
    if (!ump::isChannelVoice(m)) return false;
    switch (ump::status(m)) {
    case ump::Status::PolyPressure: return true;
    case ump::Status::RegisteredPerNoteController:
    case ump::Status::AssignablePerNoteController:
    case ump::Status::PerNotePitchBend:
    case ump::Status::PerNoteManagement: return ump::messageType(m) == ump::MessageType::Midi2ChannelVoice;
    default: return false;
    }
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
    params_     = clamped(p);
    params_.dry = p.dry;
    remap_.setShift(shift79(params_));
}

std::size_t RetuneCore::process(const uw::MidiMessage& in, uw::MidiMessage* out) noexcept {
    uw::MidiMessage   shifted[ump::NoteRemap::kMaxOut];
    const std::size_t n = remap_.process(in, shifted);
    std::size_t       count = 0;

    if (ump::isChannelVoice(in)) {
        const int group = (in.words[0] >> 24) & 0xF, channel = ump::channel(in), key = ump::noteNumber(in);
        bool&     sounding = drySounding_[group][channel][key];
        if (ump::isNoteOn(in)) {
            // Its own key taken by the shifted note: no room for the original.
            bool ownKey = false;
            for (std::size_t i = 0; i < n; ++i)
                ownKey = ownKey || (ump::isNoteOn(shifted[i]) && ump::noteNumber(shifted[i]) == key &&
                                    ump::channel(shifted[i]) == channel && ((shifted[i].words[0] >> 24) & 0xF) == group);
            const bool send = params_.dry && !ownKey;
            if (sounding && !send) out[count++] = noteOffFor(in);   // a dry note retriggered away
            if (send) out[count++] = in;
            sounding = send;
        } else if (ump::isNoteOff(in)) {
            if (sounding) out[count++] = in;
            sounding = false;
        } else if (isPerNote(in)) {
            if (sounding) out[count++] = in;
        } else if (ump::status(in) == ump::Status::ControlChange && (key == 120 || key == 123)) {
            for (bool& s : drySounding_[group][channel]) s = false;   // All Sound / Notes Off: once is enough
        }
    }
    for (std::size_t i = 0; i < n; ++i) out[count++] = shifted[i];
    return count;
}

void RetuneCore::reset() noexcept {
    remap_.reset();
    for (auto& group : drySounding_)
        for (auto& channel : group)
            for (bool& s : channel) s = false;
}

} // namespace retune
