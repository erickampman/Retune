// src/plugin.cpp — Retune: the CLAP plugin around RetuneCore. A note effect with one note input
// and one note output port and three parameters (Divisions, Octaves, Steps). Hand-written CLAP
// entry point, as in PlainOldSynth and StruckSynth, so the ports can speak MIDI 2.0.
//
// The ports advertise CLAP | MIDI | MIDI2 and prefer MIDI2 (docs/design.md: MIDI 2.0 is the base case).
// Output follows input: a CLAP_EVENT_MIDI2 leaves as CLAP_EVENT_MIDI2, a CLAP_EVENT_MIDI as
// CLAP_EVENT_MIDI, and CLAP note events as CLAP note events, with the fraction as a tuning
// expression (uw-core's ClapNoteRemap; Bitwig sends plugins no MIDI 2.0).
//
// Inside clap-wrapper (AUv2, VST3) the ports prefer MIDI 1.0 instead: the wrappers can't carry
// the fraction, and the AUv2 wrapper turns MIDI 2.0 output into MIDI 1.0 with the key alone, which
// would round down rather than to nearest. Both ports have id 0, since the AUv2 wrapper sends an
// output event to the port whose id equals the event's port_index.
//
// The editor (src/UI/RetuneUI.cpp) reaches the plugin only through plugin_gui.h: its edits come
// through a ring and are applied, and told to the host, at the next process() or flush; process()
// publishes the notes held at the input and at the output (where they went) for its live view.
//
// Building with RETUNE_DIALECT_LOG on adds a log of what the host sends (its supported note
// dialects and a count of every incoming note event by kind), to learn what Bitwig, REAPER and
// others actually deliver to a note effect. The audio thread only counts; the main thread
// writes the log on deactivate and destroy.

#include <clap/clap.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "RetuneCore.h"
#include "plugin_gui.h"
#include "uw/clap/ClapNoteRemap.h"
#include "uw/ump/Ump.h"

#ifndef RETUNE_VERSION_STRING
#define RETUNE_VERSION_STRING "0.1.0"
#endif

namespace {

namespace ump = uw::ump;

// ---------------------------------------------------------------------------------------------
// Parameters (appended, never inserted: the ids are the indices)
// ---------------------------------------------------------------------------------------------

struct ParamSpec {
    const char* name;
    const char* symbol;   // state file key
    double      min, max, def;
};

constexpr ParamSpec kParams[] = {
    {"Divisions", "divisions", retune::kMinDivisions, retune::kMaxDivisions, 12},
    {"Octaves", "octaves", -retune::kMaxOctaves, retune::kMaxOctaves, 0},
    // The host sees ±(max N); RetuneCore clamps Steps to ±N when it uses it (docs/design.md).
    {"Steps", "steps", -retune::kMaxDivisions, retune::kMaxDivisions, 0},
};
constexpr uint32_t kParamCount = sizeof(kParams) / sizeof(kParams[0]);

double clampParam(uint32_t index, double value) {
    return std::clamp(std::round(value), kParams[index].min, kParams[index].max);
}

// ---------------------------------------------------------------------------------------------
// Dialect log
// ---------------------------------------------------------------------------------------------

#ifdef RETUNE_DIALECT_LOG
// Incoming note events by kind. MIDI 2.0 and MIDI 1.0 are split by channel-voice status.
enum Kind : uint32_t {
    kClapNoteOn, kClapNoteOff, kClapNoteChoke, kClapNoteExpression,
    kMidi1Base,                 // + status nibble 0x8..0xE, or 0xF for system
    kMidi2Base = kMidi1Base + 16,   // + status nibble; other UMP message types go to kMidi2Other
    kMidi2Other = kMidi2Base + 16,
    kSysex,
    kKindCount
};

const char* kindName(uint32_t k, char* buffer, size_t size) {
    static const char* const clapNames[] = {"CLAP note on", "CLAP note off", "CLAP note choke",
                                            "CLAP note expression"};
    if (k < kMidi1Base) return clapNames[k];
    if (k < kMidi2Base) std::snprintf(buffer, size, "MIDI 1.0 status 0x%X", k - kMidi1Base);
    else if (k < kMidi2Other) std::snprintf(buffer, size, "MIDI2 UMP MT 0x4 status 0x%X", k - kMidi2Base);
    else if (k == kMidi2Other) std::snprintf(buffer, size, "MIDI2 UMP, other message type");
    else std::snprintf(buffer, size, "MIDI SysEx");
    return buffer;
}

std::string logPath() {
    const char* home = std::getenv("HOME");
    std::string path = home != nullptr ? home : ".";
#ifdef __APPLE__
    path += "/Library/Logs/Retune-dialects.log";
#else
    path += "/retune-dialects.log";
#endif
    return path;
}
#endif

// ---------------------------------------------------------------------------------------------
// The editor's side: its edits in, what's held out
// ---------------------------------------------------------------------------------------------

// Editor edits (main thread) → the audio thread, as NoteGran's: process() and flush() are never
// concurrent, so one consumer.
struct GuiEdit {
    enum Kind : uint8_t { Begin, Value, End } kind;
    uint32_t index;
    double   value;
};

class GuiEditRing {
public:
    bool push(const GuiEdit& edit) {
        const uint32_t t    = tail.load(std::memory_order_relaxed);
        const uint32_t next = (t + 1) & (kCapacity - 1);
        if (next == head.load(std::memory_order_acquire)) return false;   // full: dropped, never blocks
        buffer[t] = edit;
        tail.store(next, std::memory_order_release);
        return true;
    }
    bool pop(GuiEdit& out) {
        const uint32_t h = head.load(std::memory_order_relaxed);
        if (h == tail.load(std::memory_order_acquire)) return false;
        out = buffer[h];
        head.store((h + 1) & (kCapacity - 1), std::memory_order_release);
        return true;
    }

private:
    static constexpr uint32_t kCapacity = 256;   // a power of two
    GuiEdit                   buffer[kCapacity]{};
    std::atomic<uint32_t>     head{0};
    std::atomic<uint32_t>     tail{0};
};

// The notes sounding on one side of Retune, for the editor's live view only: the core's own
// latches decide where notes go. Keyed as the events address them: port, group × 16 + channel,
// key, and a CLAP note id (-1 for MIDI). An event's -1 fields are wildcards, as in CLAP.
class Held {
public:
    void on(int port, int channel, int key, int32_t noteId, uint16_t pitch79, uint16_t velocity) {
        off(port, channel, key, -1);   // a retrigger replaces the note
        if (count_ == RT_LIVE_NOTES) remove(0);
        notes_[count_++] = {port, channel, key, noteId, pitch79, velocity};
        dirty = true;
    }
    void off(int port, int channel, int key, int32_t noteId) {
        for (uint32_t i = count_; i-- > 0;)
            if (matches(notes_[i], port, channel, key, noteId)) remove(i);
    }
    // A CLAP tuning expression: the note's pitch is its key plus `semitones`.
    void tune(int port, int channel, int key, int32_t noteId, double semitones) {
        for (uint32_t i = 0; i < count_; ++i) {
            Note& n = notes_[i];
            if (!matches(n, port, channel, key, noteId)) continue;
            n.pitch79 = uint16_t(std::clamp(std::lround((n.key + semitones) * 512.0), 0L, 0xFFFFL));
            dirty     = true;
        }
    }
    void clear() {
        if (count_ != 0) dirty = true;
        count_ = 0;
    }

    uint32_t count() const { return count_; }
    uint32_t packed(uint32_t i) const { return uint32_t(notes_[i].pitch79) << 16 | notes_[i].velocity; }

    bool dirty = true;   // changed since last published

private:
    struct Note {
        int      port, channel, key;
        int32_t  noteId;
        uint16_t pitch79, velocity;
    };
    static bool matches(const Note& n, int port, int channel, int key, int32_t noteId) {
        if (noteId != -1 && n.noteId != -1) return n.noteId == noteId;
        return (port == -1 || n.port == port) && (channel == -1 || n.channel == channel) &&
               (key == -1 || n.key == key);
    }
    void remove(uint32_t i) {
        std::memmove(notes_ + i, notes_ + i + 1, (count_ - i - 1) * sizeof(Note));
        --count_;
        dirty = true;
    }

    Note     notes_[RT_LIVE_NOTES]{};
    uint32_t count_ = 0;
};

// One UMP's effect on what's held: notes on and off, All Sound Off and All Notes Off.
void watchUmp(Held& held, int port, const uw::MidiMessage& m) {
    if (!ump::isChannelVoice(m)) return;
    const int channel = int((m.words[0] >> 24) & 0xF) * 16 + ump::channel(m);
    const int key     = ump::noteNumber(m);
    if (ump::isNoteOn(m))
        held.on(port, channel, key, -1, ump::notePitch79(m), ump::velocity16(m));
    else if (ump::isNoteOff(m))
        held.off(port, channel, key, -1);
    else if (ump::status(m) == ump::Status::ControlChange && (key == 120 || key == 123))
        held.off(port, channel, -1, -1);
}

// One event's effect on what's held, in any of the three dialects.
void watch(Held& held, const clap_event_header_t* h) {
    if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    switch (h->type) {
        case CLAP_EVENT_NOTE_ON: {
            const auto* ev = reinterpret_cast<const clap_event_note_t*>(h);
            if (ev->key < 0 || ev->key > 127) return;
            held.on(ev->port_index, ev->channel, ev->key, ev->note_id, uint16_t(ev->key << 9),
                    uint16_t(std::clamp(ev->velocity, 0.0, 1.0) * 65535.0));
            break;
        }
        case CLAP_EVENT_NOTE_OFF:
        case CLAP_EVENT_NOTE_CHOKE: {
            const auto* ev = reinterpret_cast<const clap_event_note_t*>(h);
            held.off(ev->port_index, ev->channel, ev->key, ev->note_id);
            break;
        }
        case CLAP_EVENT_NOTE_EXPRESSION: {
            const auto* ev = reinterpret_cast<const clap_event_note_expression_t*>(h);
            if (ev->expression_id == CLAP_NOTE_EXPRESSION_TUNING)
                held.tune(ev->port_index, ev->channel, ev->key, ev->note_id, ev->value);
            break;
        }
        case CLAP_EVENT_MIDI: {
            const auto* ev = reinterpret_cast<const clap_event_midi_t*>(h);
            if (ev->data[0] < 0x80 || ev->data[0] >= 0xF0) return;
            uw::MidiMessage m;
            m.words[0]  = 0x2000'0000u | (uint32_t(ev->data[0]) << 16) | (uint32_t(ev->data[1] & 0x7F) << 8) |
                          uint32_t(ev->data[2] & 0x7F);
            m.wordCount = 1;
            watchUmp(held, ev->port_index, m);
            break;
        }
        case CLAP_EVENT_MIDI2: {
            const auto* ev = reinterpret_cast<const clap_event_midi2_t*>(h);
            uw::MidiMessage m;
            std::memcpy(m.words, ev->data, sizeof(m.words));
            m.wordCount = ump::wordCountForType(uint8_t(m.words[0] >> 28));
            watchUmp(held, ev->port_index, m);
            break;
        }
        default: break;
    }
}

// What's held, for the editor (rt_live_t, packed as pitch << 16 | velocity): written whole when it
// changed, at the end of process(), under a sequence count; read by the editor's timer.
struct LiveShared {
    static constexpr std::size_t kOutBase = 1 + RT_LIVE_NOTES;   // the input's count, then its notes
    static constexpr std::size_t kWords   = kOutBase + 1 + RT_LIVE_NOTES;
    std::atomic<uint32_t>        seq{0};
    std::atomic<uint32_t>        words[kWords]{};
};

// ---------------------------------------------------------------------------------------------
// The plugin
// ---------------------------------------------------------------------------------------------

struct Plugin {
    clap_plugin_t      clap{};
    const clap_host_t* host = nullptr;

    // Parameter values: written by process/flush and by state_load; read by the main thread.
    std::atomic<double> values[kParamCount];
    std::atomic<bool>   valuesChangedByState{false};   // state_load → process: re-apply

    retune::RetuneCore core;        // MIDI 1.0 and 2.0; audio thread while active, main thread otherwise
    uw::ClapNoteRemap  clapNotes;   // CLAP note events; likewise
    std::string        scratch;
    bool               wrapped = false;   // hosted by clap-wrapper (AUv2, VST3)

    // The editor (plugin_gui.h).
    void*       gui = nullptr;   // the editor's own state, held for it
    GuiEditRing guiEdits;
    Held        heldIn, heldOut;   // audio thread while active, main thread otherwise
    LiveShared  live;

#ifdef RETUNE_DIALECT_LOG
    std::atomic<uint64_t> counts[kKindCount];
    uint32_t              hostDialects = 0;
    bool                  hostHasNotePorts = false;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

retune::Params currentParams(const Plugin* p) {
    return {int(p->values[0].load(std::memory_order_relaxed)),
            int(p->values[1].load(std::memory_order_relaxed)),
            int(p->values[2].load(std::memory_order_relaxed))};
}

void applyParams(Plugin* p) {
    const retune::Params params = currentParams(p);
    p->core.setParams(params);
    p->clapNotes.setShift(retune::shift79(params));
}

void applyParamEvent(Plugin* p, const clap_event_param_value_t* ev) {
    if (ev->param_id >= kParamCount) return;
    p->values[ev->param_id].store(clampParam(ev->param_id, ev->value), std::memory_order_relaxed);
    applyParams(p);
}

// The editor's edits, applied and told to the host (as param and gesture events at time 0, before
// anything else this block sends). True if any value changed.
bool applyGuiEdits(Plugin* p, const clap_output_events_t* out) {
    bool    changed = false;
    GuiEdit edit;
    while (p->guiEdits.pop(edit)) {
        if (edit.index >= kParamCount) continue;
        if (edit.kind == GuiEdit::Value) {
            p->values[edit.index].store(clampParam(edit.index, edit.value), std::memory_order_relaxed);
            changed = true;
        }
        if (out == nullptr) continue;
        if (edit.kind == GuiEdit::Value) {
            clap_event_param_value_t ev{};
            ev.header     = {sizeof(ev), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
            ev.param_id   = edit.index;
            ev.note_id    = -1;
            ev.port_index = -1;
            ev.channel    = -1;
            ev.key        = -1;
            ev.value      = p->values[edit.index].load(std::memory_order_relaxed);
            out->try_push(out, &ev.header);
        } else {
            clap_event_param_gesture_t ev{};
            ev.header   = {sizeof(ev), 0, CLAP_CORE_EVENT_SPACE_ID,
                           uint16_t(edit.kind == GuiEdit::Begin ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                                                : CLAP_EVENT_PARAM_GESTURE_END),
                           0};
            ev.param_id = edit.index;
            out->try_push(out, &ev.header);
        }
    }
    if (changed) applyParams(p);
    return changed;
}

// What's held, published for the editor when it changed (rt_live_read).
void publishLive(Plugin* p) {
    if (!p->heldIn.dirty && !p->heldOut.dirty) return;
    p->heldIn.dirty = p->heldOut.dirty = false;
    LiveShared&    l = p->live;
    const uint32_t s = l.seq.load(std::memory_order_relaxed);
    l.seq.store(s + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    l.words[0].store(p->heldIn.count(), std::memory_order_relaxed);
    for (uint32_t i = 0; i < p->heldIn.count(); ++i)
        l.words[1 + i].store(p->heldIn.packed(i), std::memory_order_relaxed);
    l.words[LiveShared::kOutBase].store(p->heldOut.count(), std::memory_order_relaxed);
    for (uint32_t i = 0; i < p->heldOut.count(); ++i)
        l.words[LiveShared::kOutBase + 1 + i].store(p->heldOut.packed(i), std::memory_order_relaxed);
    l.seq.store(s + 2, std::memory_order_release);
}

// The host's output list, watched on the way out so the editor sees where each note went.
struct WatchedOut {
    clap_output_events_t       events;
    const clap_output_events_t* inner;
    Held*                      held;
};

bool watchedTryPush(const clap_output_events_t* list, const clap_event_header_t* h) {
    const auto* w = static_cast<const WatchedOut*>(list->ctx);
    if (w->inner == nullptr || w->inner->try_push == nullptr || !w->inner->try_push(w->inner, h)) return false;
    watch(*w->held, h);
    return true;
}

#ifdef RETUNE_DIALECT_LOG
void count(Plugin* p, uint32_t kind) { p->counts[kind].fetch_add(1, std::memory_order_relaxed); }

void countEvent(Plugin* p, const clap_event_header_t* h) {
    switch (h->type) {
        case CLAP_EVENT_NOTE_ON:         count(p, kClapNoteOn); break;
        case CLAP_EVENT_NOTE_OFF:        count(p, kClapNoteOff); break;
        case CLAP_EVENT_NOTE_CHOKE:      count(p, kClapNoteChoke); break;
        case CLAP_EVENT_NOTE_EXPRESSION: count(p, kClapNoteExpression); break;
        case CLAP_EVENT_MIDI_SYSEX:      count(p, kSysex); break;
        case CLAP_EVENT_MIDI: {
            const uint8_t status = reinterpret_cast<const clap_event_midi_t*>(h)->data[0];
            count(p, kMidi1Base + (status >> 4));
            break;
        }
        case CLAP_EVENT_MIDI2: {
            const uint32_t w0 = reinterpret_cast<const clap_event_midi2_t*>(h)->data[0];
            count(p, (w0 >> 28) == 0x4 ? kMidi2Base + ((w0 >> 20) & 0xF) : uint32_t(kMidi2Other));
            break;
        }
        default: break;
    }
}

void writeDialectLog(Plugin* p, const char* when) {
    FILE* f = std::fopen(logPath().c_str(), "a");
    if (f == nullptr) return;
    const char* hostName = p->host != nullptr && p->host->name != nullptr ? p->host->name : "?";
    const char* hostVersion = p->host != nullptr && p->host->version != nullptr ? p->host->version : "?";
    std::fprintf(f, "--- Retune %s, %s, host \"%s\" %s\n", RETUNE_VERSION_STRING, when, hostName,
                 hostVersion);
    if (p->hostHasNotePorts)
        std::fprintf(f, "host note dialects: 0x%X (%s%s%s%s)\n", p->hostDialects,
                     (p->hostDialects & CLAP_NOTE_DIALECT_CLAP) ? "CLAP " : "",
                     (p->hostDialects & CLAP_NOTE_DIALECT_MIDI) ? "MIDI " : "",
                     (p->hostDialects & CLAP_NOTE_DIALECT_MIDI_MPE) ? "MIDI_MPE " : "",
                     (p->hostDialects & CLAP_NOTE_DIALECT_MIDI2) ? "MIDI2" : "");
    else
        std::fprintf(f, "host note dialects: host has no note-ports extension\n");
    char buffer[64];
    bool any = false;
    for (uint32_t k = 0; k < kKindCount; ++k) {
        const uint64_t n = p->counts[k].load(std::memory_order_relaxed);
        if (n == 0) continue;
        std::fprintf(f, "  %-36s %llu\n", kindName(k, buffer, sizeof(buffer)), (unsigned long long)n);
        any = true;
    }
    if (!any) std::fprintf(f, "  no note events received\n");
    std::fclose(f);
}
#endif

// ---------------------------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------------------------

bool push(const clap_output_events_t* out, const clap_event_header_t* h) {
    return out != nullptr && out->try_push != nullptr && out->try_push(out, h);
}

// Run one UMP through the core and push what comes out, in the dialect it came in.
void transposeAndPush(Plugin* p, const clap_event_header_t* in, uint16_t port,
                      const uw::MidiMessage& m, bool asMidi1,
                      const clap_output_events_t* out) {
    uw::MidiMessage results[retune::kMaxOut];
    const std::size_t n = p->core.process(m, results);
    for (std::size_t i = 0; i < n; ++i) {
        if (asMidi1) {
            clap_event_midi_t ev{};
            ev.header = {sizeof(ev), in->time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, in->flags};
            ev.port_index = port;
            if (ump::toMidi1Bytes(results[i], ev.data) == 0) continue;
            push(out, &ev.header);
        } else {
            clap_event_midi2_t ev{};
            ev.header = {sizeof(ev), in->time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI2, in->flags};
            ev.port_index = port;
            std::memcpy(ev.data, results[i].words, sizeof(ev.data));
            push(out, &ev.header);
        }
    }
}

void handleEvent(Plugin* p, const clap_event_header_t* h, const clap_output_events_t* out) {
    if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
#ifdef RETUNE_DIALECT_LOG
    countEvent(p, h);
#endif
    switch (h->type) {
        case CLAP_EVENT_PARAM_VALUE:
            applyParamEvent(p, reinterpret_cast<const clap_event_param_value_t*>(h));
            break;

        case CLAP_EVENT_MIDI2: {
            const auto* ev = reinterpret_cast<const clap_event_midi2_t*>(h);
            uw::MidiMessage m;
            std::memcpy(m.words, ev->data, sizeof(m.words));
            m.wordCount = ump::wordCountForType(uint8_t(m.words[0] >> 28));
            transposeAndPush(p, h, ev->port_index, m, false, out);
            break;
        }

        case CLAP_EVENT_MIDI: {
            const auto* ev = reinterpret_cast<const clap_event_midi_t*>(h);
            if (ev->data[0] >= 0x80 && ev->data[0] < 0xF0) {
                // Channel voice: wrap as MT 0x2 (group 0), the core's MIDI 1.0 form.
                uw::MidiMessage m;
                m.words[0]  = 0x2000'0000u | (uint32_t(ev->data[0]) << 16) |
                              (uint32_t(ev->data[1] & 0x7F) << 8) | uint32_t(ev->data[2] & 0x7F);
                m.wordCount = 1;
                transposeAndPush(p, h, ev->port_index, m, true, out);
            } else {
                push(out, h);   // system messages pass through as they are
            }
            break;
        }

        case CLAP_EVENT_NOTE_ON:
        case CLAP_EVENT_NOTE_OFF:
        case CLAP_EVENT_NOTE_CHOKE:
        case CLAP_EVENT_NOTE_EXPRESSION:
            p->clapNotes.process(h, out);
            break;

        case CLAP_EVENT_MIDI_SYSEX:
            push(out, h);   // SysEx passes through
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------------------------
// clap_plugin
// ---------------------------------------------------------------------------------------------

bool plug_init(const clap_plugin_t* plugin) {
#ifdef RETUNE_DIALECT_LOG
    Plugin* p = self(plugin);
    const auto* notePorts = static_cast<const clap_host_note_ports_t*>(
        p->host->get_extension(p->host, CLAP_EXT_NOTE_PORTS));
    if (notePorts != nullptr && notePorts->supported_dialects != nullptr) {
        p->hostHasNotePorts = true;
        p->hostDialects     = notePorts->supported_dialects(p->host);
    }
#else
    (void)plugin;
#endif
    return true;
}

void plug_destroy(const clap_plugin_t* plugin) {
    Plugin* p = self(plugin);
#ifdef RETUNE_DIALECT_LOG
    writeDialectLog(p, "destroy");
#endif
    delete p;
}

bool plug_activate(const clap_plugin_t* plugin, double, uint32_t, uint32_t) {
    Plugin* p = self(plugin);
    p->core.reset();
    p->clapNotes.reset();
    p->heldIn.clear();
    p->heldOut.clear();
    publishLive(p);
    applyParams(p);
    p->valuesChangedByState.store(false, std::memory_order_relaxed);
    return true;
}

void plug_deactivate(const clap_plugin_t* plugin) {
#ifdef RETUNE_DIALECT_LOG
    writeDialectLog(self(plugin), "deactivate");
#else
    (void)plugin;
#endif
}

bool plug_start_processing(const clap_plugin_t*) { return true; }
void plug_stop_processing(const clap_plugin_t*) {}

void plug_reset(const clap_plugin_t* plugin) {
    Plugin* p = self(plugin);
    p->core.reset();
    p->clapNotes.reset();
    p->heldIn.clear();
    p->heldOut.clear();
    publishLive(p);
}

clap_process_status plug_process(const clap_plugin_t* plugin, const clap_process_t* process) {
    Plugin* p = self(plugin);
    if (p->valuesChangedByState.exchange(false, std::memory_order_acquire))
        applyParams(p);
    applyGuiEdits(p, process->out_events);

    WatchedOut out{{nullptr, watchedTryPush}, process->out_events, &p->heldOut};
    out.events.ctx = &out;
    const clap_input_events_t* in = process->in_events;
    const uint32_t count = in != nullptr ? in->size(in) : 0;
    for (uint32_t i = 0; i < count; ++i) {
        const clap_event_header_t* h = in->get(in, i);
        watch(p->heldIn, h);
        handleEvent(p, h, &out.events);
    }
    publishLive(p);
    return CLAP_PROCESS_CONTINUE;
}

void plug_on_main_thread(const clap_plugin_t*) {}

// ---------------------------------------------------------------------------------------------
// audio-ports extension: none (a note effect)
// ---------------------------------------------------------------------------------------------

uint32_t audio_ports_count(const clap_plugin_t*, bool) { return 0; }
bool audio_ports_get(const clap_plugin_t*, uint32_t, bool, clap_audio_port_info_t*) { return false; }
const clap_plugin_audio_ports_t kAudioPorts = {audio_ports_count, audio_ports_get};

// ---------------------------------------------------------------------------------------------
// note-ports extension: one in, one out
// ---------------------------------------------------------------------------------------------

uint32_t note_ports_count(const clap_plugin_t*, bool) { return 1; }

bool note_ports_get(const clap_plugin_t* plugin, uint32_t index, bool isInput, clap_note_port_info_t* info) {
    if (index != 0) return false;
    std::memset(info, 0, sizeof(*info));
    info->id                 = 0;   // ids may overlap between inputs and outputs
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI2;
    info->preferred_dialect  = self(plugin)->wrapped ? CLAP_NOTE_DIALECT_MIDI : CLAP_NOTE_DIALECT_MIDI2;
    std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Notes in" : "Notes out");
    return true;
}

const clap_plugin_note_ports_t kNotePorts = {note_ports_count, note_ports_get};

// ---------------------------------------------------------------------------------------------
// params extension
// ---------------------------------------------------------------------------------------------

uint32_t params_count(const clap_plugin_t*) { return kParamCount; }

bool params_get_info(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
    if (index >= kParamCount) return false;
    std::memset(info, 0, sizeof(*info));
    info->id            = index;
    info->flags         = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
    info->min_value     = kParams[index].min;
    info->max_value     = kParams[index].max;
    info->default_value = kParams[index].def;
    std::snprintf(info->name, sizeof(info->name), "%s", kParams[index].name);
    return true;
}

bool params_get_value(const clap_plugin_t* plugin, clap_id id, double* out) {
    if (id >= kParamCount) return false;
    *out = self(plugin)->values[id].load(std::memory_order_relaxed);
    return true;
}

bool params_value_to_text(const clap_plugin_t*, clap_id id, double value, char* out, uint32_t size) {
    if (id >= kParamCount) return false;
    std::snprintf(out, size, "%d", int(clampParam(id, value)));
    return true;
}

bool params_text_to_value(const clap_plugin_t*, clap_id id, const char* text, double* out) {
    if (id >= kParamCount) return false;
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text) return false;
    *out = clampParam(id, value);
    return true;
}

void params_flush(const clap_plugin_t* plugin, const clap_input_events_t* in,
                  const clap_output_events_t* out) {
    // Never concurrent with process, so the core can be updated here too.
    Plugin* p = self(plugin);
    applyGuiEdits(p, out);
    const uint32_t count = in != nullptr ? in->size(in) : 0;
    for (uint32_t i = 0; i < count; ++i) {
        const clap_event_header_t* h = in->get(in, i);
        if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE)
            applyParamEvent(p, reinterpret_cast<const clap_event_param_value_t*>(h));
    }
}

const clap_plugin_params_t kParamsExt = {params_count, params_get_info, params_get_value,
                                         params_value_to_text, params_text_to_value, params_flush};

// ---------------------------------------------------------------------------------------------
// state extension: "Retune 1" then "<symbol> <value>" lines; unknown symbols are ignored
// ---------------------------------------------------------------------------------------------

bool state_save(const clap_plugin_t* plugin, const clap_ostream_t* stream) {
    Plugin* p = self(plugin);
    p->scratch = "Retune 1\n";
    char line[64];
    for (uint32_t i = 0; i < kParamCount; ++i) {
        std::snprintf(line, sizeof(line), "%s %d\n", kParams[i].symbol,
                      int(p->values[i].load(std::memory_order_relaxed)));
        p->scratch += line;
    }
    const auto* bytes = reinterpret_cast<const uint8_t*>(p->scratch.data());
    for (uint64_t done = 0; done < p->scratch.size();) {
        const int64_t written = stream->write(stream, bytes + done, p->scratch.size() - done);
        if (written <= 0) return false;
        done += uint64_t(written);
    }
    return true;
}

bool state_load(const clap_plugin_t* plugin, const clap_istream_t* stream) {
    Plugin* p = self(plugin);
    std::string text;
    char buffer[256];
    for (;;) {
        const int64_t read = stream->read(stream, buffer, sizeof(buffer));
        if (read < 0) return false;
        if (read == 0) break;
        text.append(buffer, size_t(read));
    }
    if (text.rfind("Retune ", 0) != 0) return false;   // empty, or not ours

    size_t pos = text.find('\n');
    while (pos != std::string::npos && pos + 1 < text.size()) {
        const size_t start = pos + 1;
        pos = text.find('\n', start);
        const std::string line = text.substr(start, pos == std::string::npos ? std::string::npos
                                                                              : pos - start);
        const size_t space = line.find(' ');
        if (space == std::string::npos) continue;
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (line.compare(0, space, kParams[i].symbol) == 0)
                p->values[i].store(clampParam(i, std::strtod(line.c_str() + space + 1, nullptr)),
                                   std::memory_order_relaxed);
    }
    p->valuesChangedByState.store(true, std::memory_order_release);

    const auto* hostParams = static_cast<const clap_host_params_t*>(
        p->host->get_extension(p->host, CLAP_EXT_PARAMS));
    if (hostParams != nullptr && hostParams->rescan != nullptr)
        hostParams->rescan(p->host, CLAP_PARAM_RESCAN_VALUES);
    return true;
}

const clap_plugin_state_t kStateExt = {state_save, state_load};

// ---------------------------------------------------------------------------------------------
// timer-support: the editor's (src/UI/RetuneUI.cpp)
// ---------------------------------------------------------------------------------------------

void plug_on_timer(const clap_plugin_t* plugin, clap_id timerId) { rt_gui_on_timer(plugin, timerId); }

const clap_plugin_timer_support_t kTimerExt = {plug_on_timer};

const void* plug_get_extension(const clap_plugin_t*, const char* id) {
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kAudioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &kNotePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &kParamsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &kStateExt;
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return rt_gui_extension();
    if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0) return &kTimerExt;
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// descriptor + factory + entry
// ---------------------------------------------------------------------------------------------

const char* kFeatures[] = {
    CLAP_PLUGIN_FEATURE_NOTE_EFFECT,
    CLAP_PLUGIN_FEATURE_UTILITY,
    nullptr,
};

const clap_plugin_descriptor_t kDescriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id           = "com.unlikelyware.retune",
    .name         = "Retune",
    .vendor       = "Unlikelyware",
    .url          = "https://unlikelyware.com",
    .manual_url   = "",
    .support_url  = "",
    .version      = RETUNE_VERSION_STRING,
    .description  = "Transposes notes by an interval of an equal division of the octave; "
                    "MIDI 2.0 notes keep the fraction as a Pitch 7.9 attribute.",
    .features     = kFeatures,
};

const clap_plugin_t* factory_create(const clap_plugin_factory_t*, const clap_host_t* host,
                                    const char* id) {
    if (std::strcmp(id, kDescriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (p == nullptr) return nullptr;
    p->host = host;
    // clap-wrapper's host names the plugin's real host but keeps its own url (clap_proxy.cpp).
    p->wrapped = host->url != nullptr &&
                 std::strcmp(host->url, "https://github.com/free-audio/clap-wrapper") == 0;
    for (uint32_t i = 0; i < kParamCount; ++i)
        p->values[i].store(kParams[i].def, std::memory_order_relaxed);
#ifdef RETUNE_DIALECT_LOG
    for (auto& c : p->counts) c.store(0, std::memory_order_relaxed);
#endif
    applyParams(p);
    p->clap = clap_plugin_t{
        .desc             = &kDescriptor,
        .plugin_data      = p,
        .init             = plug_init,
        .destroy          = plug_destroy,
        .activate         = plug_activate,
        .deactivate       = plug_deactivate,
        .start_processing = plug_start_processing,
        .stop_processing  = plug_stop_processing,
        .reset            = plug_reset,
        .process          = plug_process,
        .get_extension    = plug_get_extension,
        .on_main_thread   = plug_on_main_thread,
    };
    return &p->clap;
}

uint32_t factory_count(const clap_plugin_factory_t*) { return 1; }

const clap_plugin_descriptor_t* factory_get(const clap_plugin_factory_t*, uint32_t index) {
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_factory_t kFactory = {factory_count, factory_get, factory_create};

bool entry_init(const char*) { return true; }
void entry_deinit() {}

const void* entry_get_factory(const char* factoryId) {
    return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// The seam the editor calls (plugin_gui.h)
// ---------------------------------------------------------------------------------------------

extern "C" {

const clap_host_t* rt_plugin_host(const clap_plugin_t* plugin) { return self(plugin)->host; }

void  rt_plugin_set_gui(const clap_plugin_t* plugin, void* gui) { self(plugin)->gui = gui; }
void* rt_plugin_get_gui(const clap_plugin_t* plugin) { return self(plugin)->gui; }

bool rt_param_info(const clap_plugin_t*, uint32_t index, rt_param_info_t* info) {
    if (index >= kParamCount) return false;
    info->name = kParams[index].name;
    info->min  = kParams[index].min;
    info->max  = kParams[index].max;
    info->def  = kParams[index].def;
    return true;
}

double rt_param_value(const clap_plugin_t* plugin, uint32_t index) {
    return index < kParamCount ? self(plugin)->values[index].load(std::memory_order_relaxed) : 0.0;
}

void rt_param_begin_gesture(const clap_plugin_t* plugin, uint32_t index) {
    self(plugin)->guiEdits.push({GuiEdit::Begin, index, 0.0});
}

void rt_param_set_from_gui(const clap_plugin_t* plugin, uint32_t index, double v) {
    Plugin* p = self(plugin);
    if (index >= kParamCount) return;
    // Stored at once, so the editor's next read shows it; applied at the next process() or flush.
    p->values[index].store(clampParam(index, v), std::memory_order_relaxed);
    p->guiEdits.push({GuiEdit::Value, index, v});
    const auto* hostParams = static_cast<const clap_host_params_t*>(p->host->get_extension(p->host, CLAP_EXT_PARAMS));
    if (hostParams != nullptr && hostParams->request_flush != nullptr) hostParams->request_flush(p->host);
}

void rt_param_end_gesture(const clap_plugin_t* plugin, uint32_t index) {
    self(plugin)->guiEdits.push({GuiEdit::End, index, 0.0});
}

bool rt_live_read(const clap_plugin_t* plugin, rt_live_t* out) {
    LiveShared&    l = self(plugin)->live;
    const uint32_t s = l.seq.load(std::memory_order_acquire);
    if (s & 1) return false;
    uint32_t w[LiveShared::kWords];
    for (std::size_t i = 0; i < LiveShared::kWords; ++i) w[i] = l.words[i].load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    if (l.seq.load(std::memory_order_relaxed) != s) return false;
    out->inCount  = std::min<uint32_t>(w[0], RT_LIVE_NOTES);
    out->outCount = std::min<uint32_t>(w[LiveShared::kOutBase], RT_LIVE_NOTES);
    for (std::size_t i = 0; i < RT_LIVE_NOTES; ++i) {
        out->inPitch[i]     = uint16_t(w[1 + i] >> 16);
        out->inVelocity[i]  = uint16_t(w[1 + i]);
        out->outPitch[i]    = uint16_t(w[LiveShared::kOutBase + 1 + i] >> 16);
        out->outVelocity[i] = uint16_t(w[LiveShared::kOutBase + 1 + i]);
    }
    return true;
}

} // extern "C"

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init         = entry_init,
    .deinit       = entry_deinit,
    .get_factory  = entry_get_factory,
};
