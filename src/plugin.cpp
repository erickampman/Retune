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
// through a ring and are applied, and told to the host, at the next process() or flush.
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
    // Off 0, On 1: the original notes out as well as the shifted ones (RetuneCore.h, ClapDry below).
    {"Dry", "dry", 0, 1, 0},
};
constexpr uint32_t kDry = 3;
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
// The editor's edits
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

// ---------------------------------------------------------------------------------------------
// Dry for CLAP note events (RetuneCore does it for MIDI)
// ---------------------------------------------------------------------------------------------

// The original CLAP notes out as well as the shifted ones, by RetuneCore's rule: a Note On is sent
// unchanged too, unless the shifted note lands on its own key; a dry note is latched (by channel
// and key, with the host's note_id), so its Note Off, choke and expressions go out unchanged while
// it sounds. The copies carry note_id -1, leaving the host's id to the shifted note: a synth then
// tells them apart by key. A wildcard event (key and note_id both -1) reaches the dry notes as it
// is, so it isn't copied.
class ClapDry {
public:
    // Before ClapNoteRemap sees the event. `keyShift`: the shifted note's distance from its key,
    // in whole keys (as ClapNoteRemap rounds it).
    void process(const clap_event_header_t* h, bool dry, int keyShift, const clap_output_events_t* out) {
        if (h->type == CLAP_EVENT_NOTE_EXPRESSION) {
            const auto* ev = reinterpret_cast<const clap_event_note_expression_t*>(h);
            if (ev->key < 0 && ev->note_id < 0) return;
            forEach(ev->channel, ev->key, ev->note_id, [&](int channel, int key) {
                clap_event_note_expression_t copy = *ev;
                copy.note_id = -1;
                copy.channel = int16_t(channel);
                copy.key     = int16_t(key);
                out->try_push(out, &copy.header);
            });
            return;
        }
        const auto* ev = reinterpret_cast<const clap_event_note_t*>(h);
        if (h->type == CLAP_EVENT_NOTE_ON) {
            if (ev->channel < 0 || ev->channel > 15 || ev->key < 0 || ev->key > 127) return;
            Note&      n    = notes_[ev->channel][ev->key];
            const bool send = dry && keyShift != 0;   // on its own key, only the shifted note plays
            if (n.sounding && !send) pushCopy(*ev, CLAP_EVENT_NOTE_OFF, ev->channel, ev->key, out);
            if (send) pushCopy(*ev, CLAP_EVENT_NOTE_ON, ev->channel, ev->key, out);
            n = {send, ev->note_id};
            return;
        }
        // Note Off or choke.
        const bool wildcard = ev->key < 0 && ev->note_id < 0;
        forEach(ev->channel, ev->key, ev->note_id, [&](int channel, int key) {
            if (!wildcard) pushCopy(*ev, h->type, channel, key, out);
            notes_[channel][key].sounding = false;
        });
    }

    // All Sound Off or All Notes Off on a channel (as MIDI), and host reset.
    void endChannel(int channel) {
        for (Note& n : notes_[channel]) n.sounding = false;
    }
    void reset() {
        for (int c = 0; c < 16; ++c) endChannel(c);
    }

private:
    struct Note {
        bool    sounding = false;
        int32_t noteId   = -1;
    };

    // Each sounding dry note the address names: by note_id when the event has one, else by
    // channel and key, -1 matching any.
    template <typename F>
    void forEach(int channel, int key, int32_t noteId, F&& f) {
        for (int c = 0; c < 16; ++c) {
            if (channel >= 0 && channel != c) continue;
            for (int k = 0; k < 128; ++k) {
                const Note& n = notes_[c][k];
                if (!n.sounding) continue;
                if (noteId >= 0 ? n.noteId == noteId : (key < 0 || key == k)) f(c, k);
            }
        }
    }

    static void pushCopy(const clap_event_note_t& ev, uint16_t type, int channel, int key,
                         const clap_output_events_t* out) {
        clap_event_note_t copy = ev;
        copy.header.type = type;
        copy.note_id     = -1;
        copy.channel     = int16_t(channel);
        copy.key         = int16_t(key);
        out->try_push(out, &copy.header);
    }

    Note notes_[16][128];   // [channel][key]
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
    ClapDry            clapDry;     // and their originals, with Dry; likewise
    std::string        scratch;
    bool               wrapped = false;   // hosted by clap-wrapper (AUv2, VST3)

    // The editor (plugin_gui.h).
    void*       gui = nullptr;   // the editor's own state, held for it
    GuiEditRing guiEdits;

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
            int(p->values[2].load(std::memory_order_relaxed)),
            p->values[kDry].load(std::memory_order_relaxed) >= 0.5};
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
                // All Sound Off / All Notes Off end the channel's dry CLAP notes too.
                if ((ev->data[0] & 0xF0) == 0xB0 && (ev->data[1] == 120 || ev->data[1] == 123))
                    p->clapDry.endChannel(ev->data[0] & 0x0F);
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
            if (out != nullptr && out->try_push != nullptr)
                p->clapDry.process(h, p->core.params().dry, retune::shiftSemitones(p->core.params()), out);
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
    p->clapDry.reset();
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
    self(plugin)->core.reset();
    self(plugin)->clapNotes.reset();
    self(plugin)->clapDry.reset();
}

clap_process_status plug_process(const clap_plugin_t* plugin, const clap_process_t* process) {
    Plugin* p = self(plugin);
    if (p->valuesChangedByState.exchange(false, std::memory_order_acquire))
        applyParams(p);
    applyGuiEdits(p, process->out_events);

    const clap_input_events_t* in = process->in_events;
    const uint32_t count = in != nullptr ? in->size(in) : 0;
    for (uint32_t i = 0; i < count; ++i)
        handleEvent(p, in->get(in, i), process->out_events);
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
    if (id == kDry) std::snprintf(out, size, "%s", clampParam(id, value) > 0 ? "On" : "Off");
    else std::snprintf(out, size, "%d", int(clampParam(id, value)));
    return true;
}

bool params_text_to_value(const clap_plugin_t*, clap_id id, const char* text, double* out) {
    if (id >= kParamCount) return false;
    if (id == kDry && (std::strcmp(text, "On") == 0 || std::strcmp(text, "Off") == 0)) {
        *out = text[1] == 'n' ? 1.0 : 0.0;
        return true;
    }
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

} // extern "C"

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init         = entry_init,
    .deinit       = entry_deinit,
    .get_factory  = entry_get_factory,
};
