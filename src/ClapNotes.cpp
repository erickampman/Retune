#include "ClapNotes.h"

#include <algorithm>

namespace retune {

namespace {

void push(const clap_output_events_t* out, const clap_event_header_t* h) {
    if (out != nullptr && out->try_push != nullptr) out->try_push(out, h);
}

bool validKey(int key) { return key >= 0 && key <= 127; }

// Adds a fraction (1/512 semitone) to a tuning expression, within its ±120 semitone range.
void addTuning(clap_event_note_expression_t& m, int fraction79) {
    if (m.expression_id == CLAP_NOTE_EXPRESSION_TUNING)
        m.value = std::clamp(m.value + fraction79 / 512.0, -120.0, 120.0);
}

} // namespace

void ClapNotes::setParams(const Params& p) noexcept {
    keyShift_   = shiftSemitones(p);
    fraction79_ = shift79(p) - keyShift_ * 512;
}

void ClapNotes::reset() noexcept {
    for (auto& channel : latch_)
        for (auto& e : channel) e = Entry{};
}

void ClapNotes::process(const clap_event_header_t* h, const clap_output_events_t* out) noexcept {
    switch (h->type) {
        case CLAP_EVENT_NOTE_ON:
            noteOn(*reinterpret_cast<const clap_event_note_t*>(h), out);
            break;
        case CLAP_EVENT_NOTE_OFF:
        case CLAP_EVENT_NOTE_CHOKE:
            noteEnd(*reinterpret_cast<const clap_event_note_t*>(h), out);
            break;
        case CLAP_EVENT_NOTE_EXPRESSION:
            expression(*reinterpret_cast<const clap_event_note_expression_t*>(h), out);
            break;
        default:
            push(out, h);
            break;
    }
}

template <typename F>
int ClapNotes::forEachMatch(int channel, int key, std::int32_t noteId, F&& f) noexcept {
    if (channel > 15 || key > 127) return 0;
    const int c0 = channel < 0 ? 0 : channel, c1 = channel < 0 ? 15 : channel;
    const int k0 = key < 0 ? 0 : key, k1 = key < 0 ? 127 : key;
    int n = 0;
    for (int c = c0; c <= c1; ++c)
        for (int k = k0; k <= k1; ++k) {
            Entry& e = latch_[c][k];
            if (e.state == Latch::None) continue;
            // With a note_id, the latched note must have the same one; a note the host gave no
            // id to still matches by its key.
            if (noteId >= 0 && e.noteId != noteId && (key < 0 || e.noteId >= 0)) continue;
            f(c, e);
            ++n;
        }
    return n;
}

void ClapNotes::noteOn(const clap_event_note_t& ev, const clap_output_events_t* out) noexcept {
    if (ev.channel < 0 || ev.channel > 15 || !validKey(ev.key)) {
        push(out, &ev.header);   // not a playable note: leave it to the receiver
        return;
    }
    Entry& e = latch_[ev.channel][ev.key];
    const int outKey = ev.key + keyShift_;
    const bool inRange = validKey(outKey);
    if (e.state == Latch::Sounding && (!inRange || e.outKey != outKey)) {
        // Retriggered while sounding, and the new note won't land on the old key: end the old
        // one first so it can't hang.
        clap_event_note_t off = ev;
        off.header.type = CLAP_EVENT_NOTE_OFF;
        off.note_id     = e.noteId;
        off.key         = e.outKey;
        off.velocity    = 0;
        push(out, &off.header);
    }
    if (!inRange) {
        e = {Latch::Dropped, 0, 0, ev.note_id};
        return;
    }
    e = {Latch::Sounding, static_cast<std::uint8_t>(outKey), static_cast<std::int16_t>(fraction79_),
         ev.note_id};

    clap_event_note_t on = ev;
    on.key = static_cast<std::int16_t>(outKey);
    push(out, &on.header);
    if (fraction79_ != 0) {
        clap_event_note_expression_t tuning{};
        tuning.header = {sizeof(tuning), ev.header.time, CLAP_CORE_EVENT_SPACE_ID,
                         CLAP_EVENT_NOTE_EXPRESSION, ev.header.flags};
        tuning.expression_id = CLAP_NOTE_EXPRESSION_TUNING;
        tuning.note_id       = ev.note_id;
        tuning.port_index    = ev.port_index;
        tuning.channel       = ev.channel;
        tuning.key           = on.key;
        tuning.value         = fraction79_ / 512.0;
        push(out, &tuning.header);
    }
}

void ClapNotes::noteEnd(const clap_event_note_t& ev, const clap_output_events_t* out) noexcept {
    if (ev.key < 0 && ev.note_id < 0) {
        forEachMatch(ev.channel, -1, -1, [](int, Entry& e) { e = Entry{}; });
        push(out, &ev.header);
        return;
    }
    const int n = forEachMatch(ev.channel, ev.key, ev.note_id, [&](int channel, Entry& e) {
        if (e.state == Latch::Sounding) {
            clap_event_note_t m = ev;
            m.channel = static_cast<std::int16_t>(channel);
            m.key     = e.outKey;
            push(out, &m.header);
        }
        e = Entry{};
    });
    if (n != 0) return;
    // No Note On seen (it came before a reset, or before Retune started): guess the key the
    // current shift would have given it.
    clap_event_note_t m = ev;
    if (ev.key >= 0) {
        if (!validKey(ev.key + keyShift_)) return;
        m.key = static_cast<std::int16_t>(ev.key + keyShift_);
    }
    push(out, &m.header);
}

void ClapNotes::expression(const clap_event_note_expression_t& ev,
                           const clap_output_events_t* out) noexcept {
    if (ev.key >= 0 || ev.note_id >= 0) {
        const int n = forEachMatch(ev.channel, ev.key, ev.note_id, [&](int channel, Entry& e) {
            if (e.state != Latch::Sounding) return;
            clap_event_note_expression_t m = ev;
            m.channel = static_cast<std::int16_t>(channel);
            m.key     = e.outKey;
            addTuning(m, e.fraction79);
            push(out, &m.header);
        });
        if (n != 0) return;
    }
    // Wildcard, or no latched note: the current shift.
    clap_event_note_expression_t m = ev;
    if (ev.key >= 0) {
        if (!validKey(ev.key + keyShift_)) return;
        m.key = static_cast<std::int16_t>(ev.key + keyShift_);
    }
    addTuning(m, fraction79_);
    push(out, &m.header);
}

} // namespace retune
