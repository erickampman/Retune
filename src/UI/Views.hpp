// Views.hpp — Retune's two views, in NoteGran's style (its src/UI/Views.hpp). Each is handed its data
// by RetuneUI.cpp; neither talks to the plugin.
//
// - ShiftCircle: the octave as a circle of N steps, 0 at the top, 12-TET's semitones ticked
//   outside it for comparison; the shift within the octave an arc from 0 (clockwise up, anticlockwise
//   down), and in the middle the whole shift in cents. Below it, the shift in words and what MIDI 1.0
//   rounds it to. A step clicked sets Steps (the nearer way round); the dot drags round the ring.
// - HeldView: a keyboard of equal semitones over the range in play: each note held at the input
//   above it, each note sounding at the output (where Retune sent it, fraction included) lit on it,
//   and the two joined while they pair up one to one.

#ifndef RETUNE_VIEWS_HPP
#define RETUNE_VIEWS_HPP

#include "NanoVG.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

START_NAMESPACE_DGL

namespace views {

inline Color withAlpha(Color c, float a) { c.alpha = a; return c; }
inline Color lighten(Color c, float f)
{
    c.red += (1.0f - c.red) * f;
    c.green += (1.0f - c.green) * f;
    c.blue += (1.0f - c.blue) * f;
    return c;
}
const Color kBack(20, 22, 26);
const Color kBorder(62, 67, 77);
const Color kDim(96, 102, 114);
const Color kText(176, 182, 190);
const Color kBright(226, 230, 236);

inline void panel(NanoVG& vg, float w, float h)
{
    vg.beginPath(); vg.roundedRect(1.5f, 1.5f, w - 3.0f, h - 3.0f, 8.0f);
    vg.fillColor(kBack); vg.fill();
    vg.strokeColor(kBorder); vg.strokeWidth(1.5f); vg.stroke();
}

// A pitch's name: its nearest key, and the cents off it when it's not on one.
inline std::string noteName(float semitones)
{
    static const char* kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int k     = static_cast<int>(std::lround(semitones));
    const int cents = static_cast<int>(std::lround((semitones - static_cast<float>(k)) * 100.0f));
    char      text[24];
    if (cents != 0) std::snprintf(text, sizeof(text), "%s%d%+d\xC2\xA2", kNames[((k % 12) + 12) % 12], k / 12 - 1, cents);
    else std::snprintf(text, sizeof(text), "%s%d", kNames[((k % 12) + 12) % 12], k / 12 - 1);
    return text;
}

} // namespace views

// --- ShiftCircle ---------------------------------------------------------------------------------

class ShiftCircle : public NanoSubWidget
{
public:
    class Callback
    {
    public:
        virtual ~Callback() {}
        virtual void stepDragStarted() = 0;
        virtual void stepChanged(int steps) = 0;
        virtual void stepDragFinished() = 0;
    };

    ShiftCircle(Widget* parent, Callback* callback) : NanoSubWidget(parent), fCallback(callback)
    {
        fHasFont = loadSharedResources();
    }

    void setAccent(Color c) { fAccent = c; }
    void setUiScale(float s) { fUiScale = s; }
    // N, and the octaves and steps in use (Steps already clamped to ±N).
    // While a drag is on, Steps is the drag's.
    void setShift(int divisions, int octaves, int steps)
    {
        fN = std::max(divisions, 1); fOctaves = octaves;
        if (!fDragging) fSteps = steps;
        repaint();
    }

protected:
    void onNanoDisplay() override
    {
        using namespace views;
        const float w = getWidth(), h = getHeight(), s = fUiScale;
        panel(*this, w, h);
        geometry();

        // The ring, 12-TET's semitones outside it, and the N steps on it.
        beginPath(); circle(fCx, fCy, fR); strokeColor(Color(56, 60, 70)); strokeWidth(1.5f * s); stroke();
        for (int k = 0; k < 12; ++k)
        {
            const float a = angle(static_cast<float>(k) / 12.0f);
            beginPath();
            moveTo(fCx + (fR + 7.0f * s) * std::cos(a), fCy + (fR + 7.0f * s) * std::sin(a));
            lineTo(fCx + (fR + 13.0f * s) * std::cos(a), fCy + (fR + 13.0f * s) * std::sin(a));
            strokeColor(Color(78, 83, 94)); strokeWidth(1.5f * s); stroke();
        }
        const float dot = std::clamp(fR * 3.14159265f / static_cast<float>(fN) * 0.32f, 1.4f * s, 4.5f * s);
        for (int k = 0; k < fN; ++k)
        {
            float x, y;
            at(k, x, y);
            beginPath(); circle(x, y, k == 0 ? dot * 1.3f : dot);
            fillColor(k == 0 ? kText : kDim); fill();
        }

        // The shift within the octave: an arc from 0 to the step it lands on.
        const int   within = fSteps;   // −N .. +N
        const float a0 = angle(0.0f), a1 = angle(static_cast<float>(within) / static_cast<float>(fN));
        if (within != 0)
        {
            beginPath();
            if (std::abs(within) == fN) circle(fCx, fCy, fR);
            else arc(fCx, fCy, fR, a0, a1, within > 0 ? CW : CCW);
            strokeColor(withAlpha(fAccent, 0.9f)); strokeWidth(4.0f * s); stroke();
        }
        {
            float x, y;
            at(((within % fN) + fN) % fN, x, y);
            beginPath(); circle(x, y, std::max(dot * 1.9f, 5.5f * s));
            fillColor(lighten(fAccent, 0.25f)); fill();
            strokeColor(Color(240, 244, 248)); strokeWidth(1.5f * s); stroke();
        }

        if (!fHasFont) return;
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fontSize(10.0f * s);
        fillColor(kDim);
        text(fCx, fCy - fR - 20.0f * s, "0", nullptr);

        // The whole shift, in the middle.
        const double cents = 1200.0 * fOctaves + 1200.0 * fSteps / fN;
        char         line[96];
        formatCents(cents, line, sizeof(line));
        fontSize(std::min(24.0f * s, fR * 0.36f));
        fillColor(kBright);
        text(fCx, fCy - 6.0f * s, line, nullptr);
        std::snprintf(line, sizeof(line), "%+.3f semitones", cents / 100.0);
        if (cents == 0.0) std::snprintf(line, sizeof(line), "no shift");
        fontSize(std::min(11.5f * s, fR * 0.18f));
        fillColor(kText);
        text(fCx, fCy + 16.0f * s, line, nullptr);

        // In words, and what MIDI 1.0 makes of it.
        const float ty = fCy + fR + 28.0f * s;
        std::string words = std::to_string(fN) + "-EDO: ";
        if (fOctaves != 0) words += signedCount(fOctaves, "octave") + ", ";
        words += signedCount(fSteps, "step");
        fontSize(12.5f * s);
        fillColor(fAccent);
        text(w * 0.5f, ty, words.c_str(), nullptr);

        // MIDI 1.0 plays the nearest semitone: how far that is from the shift, sharp or flat.
        const long   whole = std::lround(cents / 100.0);
        const double sharp = 100.0 * static_cast<double>(whole) - cents;
        char         semis[24];
        if (whole == 0) std::snprintf(semis, sizeof(semis), "0 semitones");
        else std::snprintf(semis, sizeof(semis), "%+ld semitone%s", whole, std::labs(whole) == 1 ? "" : "s");
        if (cents == 0.0)
            std::snprintf(line, sizeof(line), "MIDI 1.0: unchanged");
        else if (std::fabs(sharp) < 0.005)
            std::snprintf(line, sizeof(line), "MIDI 1.0: %s, exact", semis);
        else
            std::snprintf(line, sizeof(line), "MIDI 1.0 rounds to %s, %.1f\xC2\xA2 %s", semis, std::fabs(sharp),
                          sharp > 0 ? "sharp" : "flat");
        fontSize(11.0f * s);
        fillColor(kDim);
        text(w * 0.5f, ty + 18.0f * s, line, nullptr);
    }

    // A press on the ring is one gesture: the step pressed (the nearer way round), or the dot where
    // it is when the press is on it, then wherever the drag takes it. A drag carries on past the
    // bottom rather than jumping to the other sign, up to a whole octave either way (±N).
    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        if (!ev.press)
        {
            if (!fDragging) return false;
            fDragging = false;
            fCallback->stepDragFinished();
            return true;
        }
        const float px = static_cast<float>(ev.pos.getX()) - fCx, py = static_cast<float>(ev.pos.getY()) - fCy;
        const float d  = std::hypot(px, py);
        if (d < fR * 0.6f || d > fR + 18.0f * fUiScale) return false;
        fDragging = true;
        fCallback->stepDragStarted();
        const int k = stepAt(px, py);
        if (k != ((fSteps % fN) + fN) % fN) goTo(2 * k <= fN ? k : k - fN);
        return true;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (!fDragging) return false;
        const int k = stepAt(static_cast<float>(ev.pos.getX()) - fCx, static_cast<float>(ev.pos.getY()) - fCy);
        // Of k, k − N and k + N, the one nearest where the dot is now.
        int best = k;
        for (int c : {k - fN, k + fN})
            if (std::abs(c - fSteps) < std::abs(best - fSteps)) best = c;
        goTo(std::clamp(best, -fN, fN));
        return true;
    }

private:
    void geometry()
    {
        const float s = fUiScale, w = getWidth(), h = getHeight();
        const float below = 52.0f * s, above = 30.0f * s;
        fR  = std::max(10.0f, std::min(w * 0.5f - 24.0f * s, (h - below - above) * 0.5f));
        fCx = w * 0.5f;
        fCy = above + (h - below - above) * 0.5f;
    }

    static float angle(float turn) { return -1.5707963f + 6.2831853f * turn; }

    // The step (0 .. N − 1) nearest a point, relative to the centre.
    int stepAt(float px, float py) const
    {
        float turn = (std::atan2(py, px) + 1.5707963f) / 6.2831853f;   // 0 at the top, clockwise
        if (turn < 0.0f) turn += 1.0f;
        return static_cast<int>(std::lround(turn * static_cast<float>(fN))) % fN;
    }
    void goTo(int steps)
    {
        if (steps == fSteps) return;
        fSteps = steps;
        fCallback->stepChanged(steps);
        repaint();
    }
    void at(int k, float& x, float& y) const
    {
        const float a = angle(static_cast<float>(k) / static_cast<float>(fN));
        x = fCx + fR * std::cos(a);
        y = fCy + fR * std::sin(a);
    }

    static void formatCents(double cents, char* out, std::size_t size)
    {
        if (std::fabs(cents - std::round(cents)) < 0.005) std::snprintf(out, size, "%+.0f\xC2\xA2", cents);
        else std::snprintf(out, size, "%+.2f\xC2\xA2", cents);
        if (cents == 0.0) std::snprintf(out, size, "0\xC2\xA2");
    }
    static std::string signedCount(int n, const char* unit)
    {
        char text[48];
        std::snprintf(text, sizeof(text), "%+d %s%s", n, unit, std::abs(n) == 1 ? "" : "s");
        return n == 0 ? std::string("0 ") + unit + "s" : std::string(text);
    }

    Callback* fCallback;
    Color     fAccent{150, 200, 110};
    bool      fHasFont = false;
    float     fUiScale = 1.0f;
    int       fN = 12, fOctaves = 0, fSteps = 0;
    bool      fDragging = false;
    float     fCx = 0, fCy = 0, fR = 10;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShiftCircle)
};

// --- HeldView ------------------------------------------------------------------------------------

class HeldView : public NanoSubWidget
{
public:
    explicit HeldView(Widget* parent) : NanoSubWidget(parent) { fHasFont = loadSharedResources(); }

    void setAccent(Color c) { fAccent = c; }
    void setUiScale(float s) { fUiScale = s; }

    // Pitches in semitones, velocities 0..1, in arrival order.
    struct Note { float pitch, velocity; };
    void setHeld(std::vector<Note> in, std::vector<Note> out)
    {
        fIn = std::move(in);
        fOut = std::move(out);
        followRange();
        repaint();
    }

protected:
    void onNanoDisplay() override
    {
        using namespace views;
        const float w = getWidth(), h = getHeight(), s = fUiScale;
        panel(*this, w, h);

        const float pad = 10.0f * s, keysH = std::min(46.0f * s, (h - 2 * pad) * 0.34f);
        const float x0 = pad, x1 = w - pad, ky = h - pad - keysH;
        const float inY = pad + 30.0f * s, outY = ky - 12.0f * s;
        const float span = fHi - fLo + 1.0f, keyW = (x1 - x0) / span;
        auto xOf = [&](float pitch) { return x0 + (pitch - fLo + 0.5f) / span * (x1 - x0); };
        auto visible = [&](float pitch) { return pitch >= fLo - 0.5f && pitch <= fHi + 0.5f; };

        // The keys: the strip pale, each output pitch lit where it falls (fraction and all) under the
        // black keys' bars, a line between neighbouring white keys, each C named.
        const float left = xOf(fLo) - keyW * 0.5f, right = xOf(fHi) + keyW * 0.5f, blackH = keysH * 0.62f;
        beginPath(); rect(left, ky, right - left, keysH); fillColor(Color(150, 156, 166)); fill();
        for (const Note& n : fOut)
        {
            if (!visible(n.pitch)) continue;
            beginPath(); rect(xOf(n.pitch) - keyW * 0.5f, ky, keyW, keysH);
            fillColor(withAlpha(fAccent, 0.55f + 0.45f * n.velocity)); fill();
        }
        for (int k = static_cast<int>(fLo); k <= static_cast<int>(fHi); ++k)
        {
            const float x = xOf(static_cast<float>(k));
            if (isBlack(k))
            {
                beginPath(); rect(x - keyW * 0.5f, ky, keyW, blackH); fillColor(Color(40, 43, 50)); fill();
            }
            else if (k > static_cast<int>(fLo) && !isBlack(k - 1))
            {
                beginPath(); moveTo(x - keyW * 0.5f, ky); lineTo(x - keyW * 0.5f, ky + keysH);
                strokeColor(Color(96, 102, 114)); strokeWidth(1.0f); stroke();
            }
            if (k % 12 == 0 && fHasFont)
            {
                char name[16];
                std::snprintf(name, sizeof(name), "C%d", k / 12 - 1);
                fontFace(NANOVG_DEJAVU_SANS_TTF);
                fontSize(std::min(10.0f * s, keyW * 1.6f));
                textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
                fillColor(Color(60, 64, 74));
                text(x - keyW * 0.5f + 2.0f, ky + keysH - 2.0f, name, nullptr);
            }
        }

        // Each input note joined to its output while they pair one to one (the shift keeps their
        // order, so the lowest in went to the lowest out).
        if (fIn.size() == fOut.size())
        {
            std::vector<float> ins, outs;
            for (const Note& n : fIn) ins.push_back(n.pitch);
            for (const Note& n : fOut) outs.push_back(n.pitch);
            std::sort(ins.begin(), ins.end());
            std::sort(outs.begin(), outs.end());
            for (std::size_t i = 0; i < ins.size(); ++i)
            {
                if (!visible(ins[i]) || !visible(outs[i])) continue;
                const float xa = xOf(ins[i]), xb = xOf(outs[i]), my = (inY + outY) * 0.5f;
                beginPath(); moveTo(xa, inY); bezierTo(xa, my, xb, my, xb, outY);
                strokeColor(withAlpha(fAccent, 0.45f)); strokeWidth(1.5f * s); stroke();
            }
        }
        const float r = std::clamp(keyW * 0.42f, 3.0f * s, 7.0f * s);
        for (const Note& n : fIn)
        {
            if (!visible(n.pitch)) continue;
            beginPath(); circle(xOf(n.pitch), inY, r);
            fillColor(withAlpha(kText, 0.55f + 0.45f * n.velocity)); fill();
        }
        for (const Note& n : fOut)
        {
            if (!visible(n.pitch)) continue;
            beginPath(); circle(xOf(n.pitch), outY, r);
            fillColor(lighten(fAccent, 0.2f)); fill();
        }

        if (!fHasFont) return;
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(11.0f * s);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(kDim);
        text(pad + 2.0f * s, inY - 18.0f * s, "In", nullptr);
        fillColor(fAccent);
        text(pad + 2.0f * s, outY - 18.0f * s, "Out", nullptr);
        // A few outputs named, cents and all.
        if (!fOut.empty() && fOut.size() <= 6)
        {
            std::string names;
            std::vector<float> outs;
            for (const Note& n : fOut) outs.push_back(n.pitch);
            std::sort(outs.begin(), outs.end());
            for (float p : outs) names += (names.empty() ? "" : "   ") + views::noteName(p);
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            fillColor(kText);
            text(w - pad - 2.0f * s, inY - 18.0f * s, names.c_str(), nullptr);
        }
        if (fIn.empty() && fOut.empty())
        {
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fontSize(12.0f * s);
            fillColor(kDim);
            text(w * 0.5f, (inY + outY) * 0.5f, "Hold notes: each shows here, and where Retune sends it", nullptr);
        }
    }

private:
    static bool isBlack(int key)
    {
        const int pc = ((key % 12) + 12) % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    // Three octaves at least, from a C, around what's held in and out; kept while nothing is.
    void followRange()
    {
        float lo = 1000.0f, hi = -1000.0f;
        for (const Note& n : fIn) lo = std::min(lo, n.pitch), hi = std::max(hi, n.pitch);
        for (const Note& n : fOut) lo = std::min(lo, n.pitch), hi = std::max(hi, n.pitch);
        if (lo > hi) return;
        lo = std::floor((lo - 5.0f) / 12.0f) * 12.0f;
        hi = std::ceil((hi + 5.0f) / 12.0f) * 12.0f;
        if (hi - lo < 36.0f)
        {
            lo = std::floor((std::round((lo + hi) / 24.0f) * 12.0f - 18.0f) / 12.0f) * 12.0f;
            hi = lo + 36.0f;
        }
        // Widen at once; narrow only when the notes are well inside.
        if (lo < fLo || hi > fHi + 1.0f || (lo > fLo + 12.0f && hi < fHi - 12.0f))
        {
            fLo = std::max(0.0f, lo);
            fHi = std::min(127.0f, hi) - 1.0f;
        }
    }

    std::vector<Note> fIn, fOut;
    Color             fAccent{150, 200, 110};
    bool              fHasFont = false;
    float             fUiScale = 1.0f;
    float             fLo = 48.0f, fHi = 83.0f;   // C3 to B5

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HeldView)
};

END_NAMESPACE_DGL

#endif // RETUNE_VIEWS_HPP
