// RetuneCore tests: the shift maths, both protocols, latching, out-of-range drop and
// pass-through (docs/design.md, phase 1). Silent unless a check fails.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "RetuneCore.h"
#include "uw/ump/Ump.h"

using namespace retune;
namespace ump = uw::ump;

namespace {

int failures = 0;

#define CHECK(expr)                                                                       \
    do {                                                                                  \
        if (!(expr)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

uw::MidiMessage packet(std::uint32_t w0, std::uint32_t w1 = 0) {
    uw::MidiMessage m;
    m.words[0]  = w0;
    m.words[1]  = w1;
    m.wordCount = ump::wordCountForType(static_cast<std::uint8_t>(w0 >> 28));
    return m;
}

std::vector<uw::MidiMessage> run(RetuneCore& core, std::uint32_t w0, std::uint32_t w1 = 0) {
    uw::MidiMessage out[kMaxOut];
    const std::size_t n = core.process(packet(w0, w1), out);
    return {out, out + n};
}

bool is(const std::vector<uw::MidiMessage>& v, std::uint32_t w0, std::uint32_t w1 = 0) {
    return v.size() == 1 && v[0].words[0] == w0 && v[0].words[1] == w1;
}

RetuneCore make(int divisions, int octaves, int steps) {
    RetuneCore core;
    core.setParams({divisions, octaves, steps});
    return core;
}

// --- the shift ----------------------------------------------------------------------------

void testShift() {
    struct Row { Params p; int shift79; int semitones; };
    const Row rows[] = {
        {{12, 0, 0}, 0, 0},
        {{12, 0, 1}, 512, 1},
        {{12, 1, 0}, 6144, 12},
        {{12, -2, -5}, -29 * 512, -29},
        {{36, 0, 1}, 171, 0},          // 1/3 semitone
        {{36, 0, -1}, -171, 0},
        {{36, 0, 20}, 3413, 7},        // 6.667 semitones
        {{19, 0, 1}, 323, 1},          // 0.632
        {{31, -1, 5}, -5153, -10},     // -10.065
        {{24, 0, 1}, 256, 1},          // a half rounds away from zero...
        {{24, 0, -1}, -256, -1},       // ...both ways
        {{5, 0, 1}, 1229, 2},          // one step of 5-EDO is 2.4 semitones
        {{12, 0, 20}, 6144, 12},       // Steps clamped to +N
        {{5, 0, -9}, -6144, -12},      // and to -N
        {{200, 0, 1}, 64, 0},          // N clamped to 96
        {{0, 0, 1}, 1229, 2},          // and to 5
        {{12, 9, 0}, 4 * 6144, 48},    // Octaves clamped to ±4
        {{12, -9, 0}, -4 * 6144, -48},
    };
    for (const Row& r : rows) {
        CHECK(shift79(r.p) == r.shift79);
        CHECK(shiftSemitones(r.p) == r.semitones);
    }
    const Params c = clamped({12, 0, 20});
    CHECK(c.divisions == 12 && c.octaves == 0 && c.steps == 12);
}

// Every N and step: the MIDI 2.0 Note On lands within 1/1024 semitone of the exact pitch.
void testEveryDivision() {
    for (int n = kMinDivisions; n <= kMaxDivisions; ++n)
        for (int s = -n; s <= n; ++s) {
            RetuneCore core = make(n, 0, s);
            const auto out  = run(core, 0x40903C00, 0x8000'0000);
            CHECK(out.size() == 1);
            if (out.size() != 1) continue;
            const int    pitch = ump::notePitch79(out[0]);
            const double exact = 512.0 * (60 + 12.0 * s / n);
            CHECK(std::fabs(pitch - exact) <= 0.5);
            CHECK(ump::noteNumber(out[0]) == pitch >> 9);
            CHECK((out[0].words[0] & 0xFF) == 3);
        }
}

// --- MIDI 1.0 -------------------------------------------------------------------------------

void testMidi1() {
    RetuneCore core = make(12, 0, 7);
    CHECK(is(run(core, 0x20903C64), 0x20904364));          // 60 -> 67, velocity kept
    CHECK(is(run(core, 0x20803C40), 0x20804340));          // its Note Off
    CHECK(is(run(core, 0x20903C64), 0x20904364));
    CHECK(is(run(core, 0x20903C00), 0x20904300));          // velocity-0 Note On is a Note Off

    RetuneCore third = make(36, 0, 1);                      // 1/3 semitone rounds to 0
    CHECK(is(run(third, 0x20903C64), 0x20903C64));

    RetuneCore down = make(31, -1, 5);                      // -10.065 -> -10
    CHECK(is(run(down, 0x25933C64), 0x25933264));          // group 5, channel 3 kept
}

// --- MIDI 2.0 -------------------------------------------------------------------------------

void testMidi2() {
    RetuneCore core = make(36, 0, 1);                       // +171/512
    CHECK(is(run(core, 0x40903C00, 0x8000'0000), 0x40903C03, 0x8000'0000u + (60u << 9) + 171));
    CHECK(is(run(core, 0x40803C00, 0x4000'0000), 0x40803C00, 0x4000'0000));   // Off: same key

    // An incoming Pitch 7.9 attribute (61.5) is the starting point.
    CHECK(is(run(core, 0x40903D03, 0x8000'7B00), 0x40903D03, 0x8000'7B00u + 171));

    RetuneCore up = make(36, 0, 20);                        // +6.667: 60 -> 66.667, key 66
    CHECK(is(run(up, 0x40903C00, 0x8000'0000), 0x40904203, 0x8000'0000u + (60u << 9) + 3413));
    CHECK(is(run(up, 0x40803C00, 0x0000'0000), 0x40804200, 0x0000'0000));

    RetuneCore below = make(36, 0, -1);                     // 60 - 1/3: key 59
    CHECK(is(run(below, 0x40903C00, 0x8000'0000), 0x40903B03, 0x8000'0000u + (60u << 9) - 171));
}

// --- latching -------------------------------------------------------------------------------

void testHeldNoteParamChange() {
    RetuneCore core = make(12, 0, 2);
    CHECK(is(run(core, 0x20903C64), 0x20903E64));          // 60 -> 62
    core.setParams({12, 0, 5});
    CHECK(is(run(core, 0x20803C40), 0x20803E40));          // still 62
    CHECK(is(run(core, 0x20903C64), 0x20904164));          // new notes: 65

    RetuneCore m2 = make(36, 0, 20);                        // key 66
    CHECK(run(m2, 0x40903C00, 0x8000'0000).size() == 1);
    m2.setParams({36, 0, -20});
    CHECK(is(run(m2, 0x40603C00, 0x1234'5678), 0x40604200, 0x1234'5678));   // per-note bend
    CHECK(is(run(m2, 0x40803C00, 0), 0x40804200, 0));
}

void testPerNoteMessages() {
    RetuneCore core = make(12, 0, 3);
    CHECK(run(core, 0x40903C00, 0x8000'0000).size() == 1);   // 60 -> 63
    CHECK(is(run(core, 0x40603C00, 0x9000'0000), 0x40603F00, 0x9000'0000));   // pitch bend
    CHECK(is(run(core, 0x40003C07, 0x1111'1111), 0x40003F07, 0x1111'1111));   // registered
    CHECK(is(run(core, 0x40103C10, 0x2222'2222), 0x40103F10, 0x2222'2222));   // assignable
    CHECK(is(run(core, 0x40F03C03, 0), 0x40F03F03, 0));                         // management
    CHECK(is(run(core, 0x40A03C00, 0x3333'3333), 0x40A03F00, 0x3333'3333));   // poly pressure

    RetuneCore m1 = make(12, 0, 3);
    CHECK(run(m1, 0x20903C64).size() == 1);
    CHECK(is(run(m1, 0x20A03C50), 0x20A03F50));             // MIDI 1.0 poly pressure
}

// A host's Pitch 7.25 (registered per-note controller 3) on a shifted note moves by the note's
// full shift, not only its whole-semitone key change.
void testPitch725() {
    RetuneCore third = make(36, 0, 1);                      // +171/512: key stays 60
    CHECK(run(third, 0x40903C00, 0x8000'0000).size() == 1);
    CHECK(is(run(third, 0x40003C03, 60u << 25), 0x40003C03, (60u << 25) + (171u << 16)));

    RetuneCore up = make(36, 0, 20);                        // +3413/512: key 66
    CHECK(run(up, 0x40903C00, 0x8000'0000).size() == 1);
    up.setParams({12, 0, 0});                               // latched: the change doesn't apply
    const std::uint32_t v = (60u << 25) | (1u << 24);       // 60.5
    CHECK(is(run(up, 0x40003C03, v), 0x40004203, v + (3413u << 16)));

    RetuneCore unlatched = make(36, 0, -20);                // -3413/512: key 53 (floor)
    CHECK(is(run(unlatched, 0x40003C03, 60u << 25), 0x40003503, (60u << 25) - (3413u << 16)));

    RetuneCore m1 = make(12, 0, 2);                         // MIDI 1.0 Note On: shift 2 × 512
    CHECK(run(m1, 0x20903C64).size() == 1);
    CHECK(is(run(m1, 0x40003C03, 60u << 25), 0x40003E03, 62u << 25));
}

void testRetrigger() {
    RetuneCore core = make(12, 0, 2);
    CHECK(is(run(core, 0x20903C64), 0x20903E64));          // 60 -> 62
    CHECK(is(run(core, 0x20903C50), 0x20903E50));          // same key again: no extra Off
    core.setParams({12, 0, 5});
    const auto out = run(core, 0x20903C64);                 // retrigger, now -> 65
    CHECK(out.size() == 2);
    if (out.size() == 2) {
        CHECK(out[0].words[0] == 0x20803E00);              // Off for 62 first
        CHECK(out[1].words[0] == 0x20904164);
    }
    CHECK(is(run(core, 0x20803C40), 0x20804140));          // the Off follows the new key

    RetuneCore m2 = make(36, 0, 20);                        // key 66
    CHECK(run(m2, 0x41923C03, 0x8000'7900).size() == 1);   // group 1, ch 2, 60.5 -> key 67
    m2.setParams({36, 0, 0});
    const auto out2 = run(m2, 0x41923C00, 0x8000'0000);
    CHECK(out2.size() == 2);
    if (out2.size() == 2) {
        CHECK(out2[0].words[0] == 0x41824300 && out2[0].words[1] == 0);   // Off 67, no attribute
        CHECK(out2[1].words[0] == 0x41923C03);
    }
}

// --- out of range ---------------------------------------------------------------------------

void testOutOfRange() {
    RetuneCore core = make(12, 4, 0);                       // +48
    CHECK(run(core, 0x20906464).empty());                   // 100 -> 148: dropped
    CHECK(run(core, 0x20A06450).empty());                   // its pressure too
    core.setParams({12, 0, 0});
    CHECK(run(core, 0x20806440).empty());                   // and its Off, after a change
    CHECK(is(run(core, 0x20806440), 0x20806440));          // a second Off is unlatched
    CHECK(is(run(core, 0x20903C64), 0x20903C64));

    RetuneCore top = make(12, 0, 1);
    CHECK(run(top, 0x40907F00, 0x8000'0000).empty());       // 127 + 1 (MIDI 2.0)
    CHECK(run(top, 0x40607F00, 0x8000'0000).empty());
    CHECK(run(top, 0x40807F00, 0).empty());

    RetuneCore bottom = make(36, 0, -1);
    CHECK(run(bottom, 0x40900000, 0x8000'0000).empty());    // 0 - 1/3

    // A retrigger whose new note is out of range still ends the old one.
    RetuneCore re = make(12, 0, 0);
    CHECK(is(run(re, 0x20907064), 0x20907064));             // 112
    re.setParams({12, 2, 0});                               // +24 -> 136
    const auto out = run(re, 0x20907064);
    CHECK(out.size() == 1 && out[0].words[0] == 0x20807000);
    CHECK(run(re, 0x20807040).empty());
}

// --- everything else ------------------------------------------------------------------------

void testPassThrough() {
    RetuneCore core = make(12, 1, 3);
    const std::uint32_t one[] = {
        0x20B00740,  // CC 7
        0x20E00040,  // pitch bend
        0x20C00500,  // program change
        0x20D04000,  // channel pressure
        0x10F80000,  // clock
        0x00000000,  // utility NOOP
    };
    for (std::uint32_t w : one) CHECK(is(run(core, w), w));
    CHECK(is(run(core, 0x40B00700, 0x1234'5678), 0x40B00700, 0x1234'5678));  // MIDI 2.0 CC
    CHECK(is(run(core, 0x40E00000, 0x8000'0000), 0x40E00000, 0x8000'0000));  // MIDI 2.0 bend
    CHECK(is(run(core, 0x30020102, 0x0000'0000), 0x30020102, 0x0000'0000));  // SysEx7
}

void testAllNotesOffAndReset() {
    RetuneCore core = make(12, 0, 2);
    CHECK(is(run(core, 0x20903C64), 0x20903E64));          // ch 0: 60 -> 62
    CHECK(is(run(core, 0x20913C64), 0x20913E64));          // ch 1: 60 -> 62
    core.setParams({12, 0, 5});
    CHECK(is(run(core, 0x20B07B00), 0x20B07B00));          // All Notes Off, ch 0: passes
    CHECK(is(run(core, 0x20803C40), 0x20804140));          // ch 0 forgot: current shift
    CHECK(is(run(core, 0x20813C40), 0x20813E40));          // ch 1 kept its latch

    CHECK(is(run(core, 0x40903C00, 0x8000'0000), 0x40904103, 0x8000'0000u + (65u << 9)));
    CHECK(is(run(core, 0x40B07800, 0), 0x40B07800, 0));    // MIDI 2.0 All Sound Off
    core.setParams({12, 0, 2});
    CHECK(is(run(core, 0x40803C00, 0), 0x40803E00, 0));    // forgot: current shift (62)

    CHECK(is(run(core, 0x20903C64), 0x20903E64));
    core.setParams({12, 0, 7});
    core.reset();
    CHECK(is(run(core, 0x20803C40), 0x20804340));          // reset forgot it: current (67)

    // Unlatched MIDI 2.0 Off: the key the current shift gives a Note On (floor), not rounded.
    RetuneCore m2 = make(36, 0, -20);                       // -6.667: 60 -> 53.333, key 53
    CHECK(is(run(m2, 0x40803C00, 0), 0x40803500, 0));
}

} // namespace

int main() {
    testShift();
    testEveryDivision();
    testMidi1();
    testMidi2();
    testHeldNoteParamChange();
    testPerNoteMessages();
    testPitch725();
    testRetrigger();
    testOutOfRange();
    testPassThrough();
    testAllNotesOffAndReset();
    if (failures != 0) {
        std::fprintf(stderr, "RetuneCore: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("RetuneCore tests passed\n");
    return 0;
}
