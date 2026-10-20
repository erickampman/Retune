// RetuneClapTests: drives the built Retune.clap through the CLAP C API, with no DAW: dlopen,
// clap_entry, factory, then MIDI 1.0 and MIDI 2.0 events in and the output events checked.
// RetuneCoreTests covers the transpose rules; this covers the CLAP side: the descriptor, note
// ports and dialects, parameters (events, flush, text), state, sample offsets and reset.
//
// Usage: retune-clap-tests /path/to/Retune.clap. Silent unless a check fails.

#include <clap/clap.h>

#include <dlfcn.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "uw/ump/Ump.h"

namespace {

int failures = 0;

#define CHECK(expr)                                                                       \
    do {                                                                                  \
        if (!(expr)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

// --- host ------------------------------------------------------------------------------------

int gRescans = 0;
void hostRescan(const clap_host_t*, clap_param_rescan_flags) { ++gRescans; }
void hostClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hostRequestFlush(const clap_host_t*) {}
const clap_host_params_t kHostParams = {hostRescan, hostClear, hostRequestFlush};

uint32_t hostDialects(const clap_host_t*) { return CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI2; }
void hostNotePortsRescan(const clap_host_t*, uint32_t) {}
const clap_host_note_ports_t kHostNotePorts = {hostDialects, hostNotePortsRescan};

const void* hostGetExtension(const clap_host_t*, const char* id) {
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &kHostParams;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &kHostNotePorts;
    return nullptr;
}
void hostNoop(const clap_host_t*) {}

const clap_host_t kHost = {
    .clap_version     = CLAP_VERSION_INIT,
    .host_data        = nullptr,
    .name             = "retune-clap-tests",
    .vendor           = "Unlikelyware",
    .url              = "",
    .version          = "0",
    .get_extension    = hostGetExtension,
    .request_restart  = hostNoop,
    .request_process  = hostNoop,
    .request_callback = hostNoop,
};

// --- events ----------------------------------------------------------------------------------

// Owns copies of the input events, in order.
struct Input {
    std::vector<std::vector<uint8_t>> storage;

    void add(const clap_event_header_t& h) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&h);
        storage.emplace_back(bytes, bytes + h.size);
    }
    void midi1(uint32_t time, uint8_t s, uint8_t d1, uint8_t d2) {
        clap_event_midi_t ev{};
        ev.header = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
        ev.data[0] = s; ev.data[1] = d1; ev.data[2] = d2;
        add(ev.header);
    }
    void midi2(uint32_t time, uint32_t w0, uint32_t w1 = 0) {
        clap_event_midi2_t ev{};
        ev.header = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI2, 0};
        ev.data[0] = w0; ev.data[1] = w1;
        add(ev.header);
    }
    void param(uint32_t time, clap_id id, double value) {
        clap_event_param_value_t ev{};
        ev.header   = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        ev.param_id = id;
        ev.note_id  = -1; ev.port_index = -1; ev.channel = -1; ev.key = -1;
        ev.value    = value;
        add(ev.header);
    }

    static uint32_t size(const clap_input_events_t* list) {
        return uint32_t(static_cast<const Input*>(list->ctx)->storage.size());
    }
    static const clap_event_header_t* get(const clap_input_events_t* list, uint32_t i) {
        return reinterpret_cast<const clap_event_header_t*>(
            static_cast<const Input*>(list->ctx)->storage[i].data());
    }
};

struct Output {
    struct Event { uint32_t time; uint16_t type; uint32_t words[4]; uint8_t bytes[3]; };
    std::vector<Event> events;

    static bool tryPush(const clap_output_events_t* list, const clap_event_header_t* h) {
        auto* self = static_cast<Output*>(list->ctx);
        Event e{h->time, h->type, {}, {}};
        if (h->type == CLAP_EVENT_MIDI2)
            std::memcpy(e.words, reinterpret_cast<const clap_event_midi2_t*>(h)->data, sizeof(e.words));
        else if (h->type == CLAP_EVENT_MIDI)
            std::memcpy(e.bytes, reinterpret_cast<const clap_event_midi_t*>(h)->data, 3);
        self->events.push_back(e);
        return true;
    }
};

bool isMidi1(const Output::Event& e, uint32_t time, uint8_t s, uint8_t d1, uint8_t d2) {
    return e.type == CLAP_EVENT_MIDI && e.time == time && e.bytes[0] == s && e.bytes[1] == d1 &&
           e.bytes[2] == d2;
}
bool isMidi2(const Output::Event& e, uint32_t time, uint32_t w0, uint32_t w1) {
    return e.type == CLAP_EVENT_MIDI2 && e.time == time && e.words[0] == w0 && e.words[1] == w1;
}

// --- the plugin under test -------------------------------------------------------------------

const clap_plugin_entry_t* gEntry = nullptr;
const clap_plugin_factory_t* gFactory = nullptr;

struct Instance {
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_params_t* params = nullptr;
    const clap_plugin_state_t* state = nullptr;
    const clap_plugin_note_ports_t* notePorts = nullptr;

    Instance() {
        plugin = gFactory->create_plugin(gFactory, &kHost, "com.unlikelyware.retune");
        if (plugin == nullptr || !plugin->init(plugin)) return;
        params    = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        state     = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE));
        notePorts = static_cast<const clap_plugin_note_ports_t*>(plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
        plugin->activate(plugin, 48000.0, 1, 512);
        plugin->start_processing(plugin);
    }
    ~Instance() {
        if (plugin == nullptr) return;
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    bool ok() const { return plugin != nullptr && params != nullptr && state != nullptr && notePorts != nullptr; }

    std::vector<Output::Event> process(Input& in) {
        Output out;
        const clap_input_events_t inList = {&in, Input::size, Input::get};
        const clap_output_events_t outList = {&out, Output::tryPush};
        clap_process_t p{};
        p.steady_time = -1;
        p.frames_count = 512;
        p.in_events = &inList;
        p.out_events = &outList;
        CHECK(plugin->process(plugin, &p) == CLAP_PROCESS_CONTINUE);
        return out.events;
    }

    double value(clap_id id) {
        double v = -1000;
        params->get_value(plugin, id, &v);
        return v;
    }
};

bool openLibrary(const std::string& path) {
    std::string binary = path;
    const std::string name = path.substr(path.find_last_of('/') + 1);
    if (name.size() > 5 && name.compare(name.size() - 5, 5, ".clap") == 0)
        binary = path + "/Contents/MacOS/" + name.substr(0, name.size() - 5);
    void* handle = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) return false;
    gEntry = static_cast<const clap_plugin_entry_t*>(dlsym(handle, "clap_entry"));
    if (gEntry == nullptr || !gEntry->init(path.c_str())) return false;
    gFactory = static_cast<const clap_plugin_factory_t*>(gEntry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    return gFactory != nullptr;
}

// --- tests -----------------------------------------------------------------------------------

void testDescriptor() {
    CHECK(gFactory->get_plugin_count(gFactory) == 1);
    const clap_plugin_descriptor_t* d = gFactory->get_plugin_descriptor(gFactory, 0);
    CHECK(d != nullptr && std::strcmp(d->id, "com.unlikelyware.retune") == 0);
    bool noteEffect = false;
    for (const char* const* f = d->features; *f != nullptr; ++f)
        noteEffect |= std::strcmp(*f, CLAP_PLUGIN_FEATURE_NOTE_EFFECT) == 0;
    CHECK(noteEffect);
}

void testNotePorts() {
    Instance r;
    CHECK(r.ok());
    if (!r.ok()) return;
    for (bool input : {true, false}) {
        CHECK(r.notePorts->count(r.plugin, input) == 1);
        clap_note_port_info_t info{};
        CHECK(r.notePorts->get(r.plugin, 0, input, &info));
        CHECK(info.supported_dialects == (CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI2));
        CHECK(info.preferred_dialect == CLAP_NOTE_DIALECT_MIDI2);
    }
    const auto* audio = static_cast<const clap_plugin_audio_ports_t*>(
        r.plugin->get_extension(r.plugin, CLAP_EXT_AUDIO_PORTS));
    CHECK(audio != nullptr && audio->count(r.plugin, true) == 0 && audio->count(r.plugin, false) == 0);
}

void testParams() {
    Instance r;
    if (!r.ok()) return;
    CHECK(r.params->count(r.plugin) == 3);
    const char* names[] = {"Divisions", "Octaves", "Steps"};
    const double ranges[][3] = {{1, 96, 12}, {-4, 4, 0}, {-96, 96, 0}};
    for (uint32_t i = 0; i < 3; ++i) {
        clap_param_info_t info{};
        CHECK(r.params->get_info(r.plugin, i, &info));
        CHECK(info.id == i && std::strcmp(info.name, names[i]) == 0);
        CHECK(info.min_value == ranges[i][0] && info.max_value == ranges[i][1] &&
              info.default_value == ranges[i][2]);
        CHECK((info.flags & CLAP_PARAM_IS_STEPPED) != 0);
        CHECK(r.value(i) == ranges[i][2]);
    }
    char text[64];
    double v = 0;
    CHECK(r.params->value_to_text(r.plugin, 2, -7.0, text, sizeof(text)) && std::strcmp(text, "-7") == 0);
    CHECK(r.params->text_to_value(r.plugin, 2, "-7", &v) && v == -7.0);
    CHECK(r.params->text_to_value(r.plugin, 0, "500", &v) && v == 96.0);   // clamped
    CHECK(!r.params->text_to_value(r.plugin, 0, "lots", &v));

    // flush (no processing): changes the value and the transpose.
    Input flushIn;
    flushIn.param(0, 2, 7);
    const clap_input_events_t list = {&flushIn, Input::size, Input::get};
    r.params->flush(r.plugin, &list, nullptr);
    CHECK(r.value(2) == 7);
    Input in;
    in.midi1(0, 0x90, 60, 100);
    const auto out = r.process(in);
    CHECK(out.size() == 1 && isMidi1(out[0], 0, 0x90, 67, 100));
}

void testMidi1() {
    Instance r;
    if (!r.ok()) return;
    Input in;
    in.param(0, 2, 2);                    // Steps 2 (12-EDO): +2, before the note
    in.midi1(10, 0x90, 60, 100);
    in.midi1(20, 0xB0, 7, 64);            // CC passes through
    in.midi1(30, 0xF8, 0, 0);             // clock passes through
    in.midi1(40, 0x80, 60, 0);
    const auto out = r.process(in);
    CHECK(out.size() == 4);
    if (out.size() == 4) {
        CHECK(isMidi1(out[0], 10, 0x90, 62, 100));
        CHECK(isMidi1(out[1], 20, 0xB0, 7, 64));
        CHECK(out[2].type == CLAP_EVENT_MIDI && out[2].time == 30 && out[2].bytes[0] == 0xF8);
        CHECK(isMidi1(out[3], 40, 0x80, 62, 0));
    }
}

void testMidi2() {
    Instance r;
    if (!r.ok()) return;
    Input in;
    in.param(0, 0, 36);                   // 36-EDO, +1 step: +171/512
    in.param(0, 2, 1);
    in.midi2(5, 0x40903C00, 0x8000'0000);
    in.midi2(6, 0x40B00700, 0x1234'5678);
    in.midi2(7, 0x40803C00, 0x4000'0000);
    const auto out = r.process(in);
    CHECK(out.size() == 3);
    if (out.size() == 3) {
        CHECK(isMidi2(out[0], 5, 0x40903C03, 0x8000'0000u + (60u << 9) + 171));
        CHECK(isMidi2(out[1], 6, 0x40B00700, 0x1234'5678));
        CHECK(isMidi2(out[2], 7, 0x40803C00, 0x4000'0000));
    }
}

void testHeldNoteAcrossBlocks() {
    Instance r;
    if (!r.ok()) return;
    Input a;
    a.param(0, 1, 1);                     // +1 octave
    a.midi2(0, 0x40903C00, 0x8000'0000);
    const auto outA = r.process(a);
    CHECK(outA.size() == 1 && outA[0].words[0] == 0x40904803);   // 72, attribute type 3

    Input b;
    b.param(0, 1, -1);                    // changed while the note is held
    b.midi2(100, 0x40803C00, 0);
    b.midi2(200, 0x40903C00, 0x8000'0000);
    const auto outB = r.process(b);
    CHECK(outB.size() == 2);
    if (outB.size() == 2) {
        CHECK(isMidi2(outB[0], 100, 0x40804800, 0));              // Off on 72
        CHECK(outB[1].time == 200 && outB[1].words[0] == 0x40903003);   // new note: 48
    }
}

void testOutOfRangeAndReset() {
    Instance r;
    if (!r.ok()) return;
    Input in;
    in.param(0, 1, 4);                    // +48
    in.midi1(0, 0x90, 100, 100);          // 148: dropped
    in.midi1(1, 0x80, 100, 0);
    CHECK(r.process(in).empty());

    Input held;
    held.param(0, 1, 0);
    held.param(0, 2, 3);
    held.midi1(0, 0x90, 60, 100);         // 63
    CHECK(r.process(held).size() == 1);
    r.plugin->reset(r.plugin);            // forgets the latch
    Input after;
    after.param(0, 2, 5);
    after.midi1(0, 0x80, 60, 0);          // unlatched: current shift, 65
    const auto out = r.process(after);
    CHECK(out.size() == 1 && isMidi1(out[0], 0, 0x80, 65, 0));
}

struct Sink {
    std::string bytes;
    static int64_t write(const clap_ostream_t* s, const void* buffer, uint64_t size) {
        static_cast<Sink*>(s->ctx)->bytes.append(static_cast<const char*>(buffer), size_t(size));
        return int64_t(size);
    }
};
struct Source {
    std::string bytes;
    size_t pos = 0;
    static int64_t read(const clap_istream_t* s, void* buffer, uint64_t size) {
        auto* self = static_cast<Source*>(s->ctx);
        const size_t n = std::min<size_t>(size_t(size), self->bytes.size() - self->pos);
        std::memcpy(buffer, self->bytes.data() + self->pos, n);
        self->pos += n;
        return int64_t(n);
    }
};

void testState() {
    Sink sink;
    {
        Instance r;
        if (!r.ok()) return;
        Input in;
        in.param(0, 0, 19);
        in.param(0, 1, -2);
        in.param(0, 2, 5);
        r.process(in);
        const clap_ostream_t os = {&sink, Sink::write};
        CHECK(r.state->save(r.plugin, &os));
    }
    CHECK(sink.bytes == "Retune 1\ndivisions 19\noctaves -2\nsteps 5\n");

    Instance r;
    if (!r.ok()) return;
    gRescans = 0;
    Source source{sink.bytes};
    const clap_istream_t is = {&source, Source::read};
    CHECK(r.state->load(r.plugin, &is));
    CHECK(gRescans == 1);
    CHECK(r.value(0) == 19 && r.value(1) == -2 && r.value(2) == 5);
    Input in;
    in.midi1(0, 0x90, 60, 100);           // 12 × (-2) + 12 × 5 / 19 = -20.84 -> -21: 39
    const auto out = r.process(in);
    CHECK(out.size() == 1 && isMidi1(out[0], 0, 0x90, 39, 100));

    Source bad{"Something else\n"};
    const clap_istream_t badStream = {&bad, Source::read};
    CHECK(!r.state->load(r.plugin, &badStream));
    Source empty{""};
    const clap_istream_t emptyStream = {&empty, Source::read};
    CHECK(!r.state->load(r.plugin, &emptyStream));
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: retune-clap-tests /path/to/Retune.clap\n");
        return 2;
    }
    if (!openLibrary(argv[1])) {
        std::fprintf(stderr, "can't load %s\n", argv[1]);
        return 1;
    }
    testDescriptor();
    testNotePorts();
    testParams();
    testMidi1();
    testMidi2();
    testHeldNoteAcrossBlocks();
    testOutOfRangeAndReset();
    testState();
    gEntry->deinit();
    if (failures != 0) {
        std::fprintf(stderr, "Retune CLAP: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("Retune CLAP tests passed\n");
    return 0;
}
