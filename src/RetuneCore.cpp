#include "RetuneCore.h"

#include <algorithm>

namespace retune {

namespace {

// num / den rounded to nearest, halves away from zero (den > 0).
int roundDiv(long num, long den) noexcept {
    return static_cast<int>(num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den));
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
    params_ = clamped(p);
    remap_.setShift(shift79(params_));
}

} // namespace retune
