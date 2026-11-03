// Views.hpp — Retune's dial, in NoteGran's style (its src/UI/Views.hpp). Handed its values by
// RetuneUI.cpp; it tells the editor of drags and never talks to the plugin.
//
// ShiftDial: all three parameters as rings round one centre.
// - Divisions, outside: an arc of 270° with its gap at the bottom, 1 at the lower left to 96 at the
//   lower right, the common EDOs marked.
// - Steps, the middle ring: the octave as N steps, 0 at the top, 12-TET's semitones ticked outside
//   it; an arc from 0 to where Steps lands (clockwise up, anticlockwise down).
// - Octaves, inside: an arc of 270° like Divisions', its gap at the bottom: −4 at the lower left,
//   0 at the top, +4 at the lower right.
// Each ring is named just inside its top, and each handle carries its value. In the middle the whole shift in cents; in the gap below, the
// shift in words and what MIDI 1.0 rounds it to.
//
// A ring's handle drags round it (Steps carries on past the bottom to ±N rather than flipping sign;
// a drag into an arc's gap stays at the end it went past); a press elsewhere on a ring jumps there first; a double-click puts it back to
// its default; the scroll wheel moves it a step. Each press, drag and release (and each scroll step)
// is one gesture.

#ifndef RETUNE_VIEWS_HPP
#define RETUNE_VIEWS_HPP

#include "NanoVG.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

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
const Color kTrack(56, 60, 70);
const Color kDim(96, 102, 114);
const Color kText(176, 182, 190);
const Color kBright(226, 230, 236);

constexpr float kPi = 3.14159265f, kTau = 6.2831853f;

} // namespace views

class ShiftDial : public NanoSubWidget
{
public:
    // The rings, by the parameter each sets (plugin_gui.h's RT_PARAM_*).
    enum Ring { kDivisions = 0, kOctaves = 1, kSteps = 2, kNone = -1 };

    class Callback
    {
    public:
        virtual ~Callback() {}
        virtual void dialDragStarted(int ring) = 0;
        virtual void dialChanged(int ring, int value) = 0;
        virtual void dialDragFinished(int ring) = 0;
    };

    ShiftDial(Widget* parent, Callback* callback) : NanoSubWidget(parent), fCallback(callback)
    {
        fHasFont = loadSharedResources();
    }

    void setAccent(Color c) { fAccent = c; }
    void setUiScale(float s) { fUiScale = s; }
    // N, and the octaves and steps in use (Steps already clamped to ±N). A ring being dragged keeps
    // the drag's value.
    void setShift(int divisions, int octaves, int steps)
    {
        if (fDragging != kDivisions) fN = std::clamp(divisions, 1, 96);
        if (fDragging != kOctaves) fOctaves = octaves;
        if (fDragging != kSteps) fSteps = steps;
        fSteps = std::clamp(fSteps, -fN, fN);
        repaint();
    }

protected:
    void onNanoDisplay() override
    {
        using namespace views;
        const float w = getWidth(), h = getHeight(), s = fUiScale;
        beginPath(); roundedRect(1.5f, 1.5f, w - 3.0f, h - 3.0f, 8.0f);
        fillColor(kBack); fill();
        strokeColor(kBorder); strokeWidth(1.5f); stroke();
        geometry();

        drawDivisions();
        drawSteps();
        drawOctaves();
        if (!fHasFont) return;

        // Each ring named, just inside its top.
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fontSize(10.5f * s);
        fillColor(kDim);
        text(fCx, fCy - fRDiv + 20.0f * s, "Divisions", nullptr);
        text(fCx, fCy - fR + 22.0f * s, "Steps", nullptr);
        text(fCx, fCy - fROct + 20.0f * s, "Octaves", nullptr);

        // The whole shift, in the middle.
        const double cents = 1200.0 * fOctaves + 1200.0 * fSteps / fN;
        char         line[96];
        if (cents == 0.0) std::snprintf(line, sizeof(line), "0\xC2\xA2");
        else if (std::fabs(cents - std::round(cents)) < 0.005) std::snprintf(line, sizeof(line), "%+.0f\xC2\xA2", cents);
        else std::snprintf(line, sizeof(line), "%+.2f\xC2\xA2", cents);
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fontSize(std::min(24.0f * s, fROct * 0.3f));
        fillColor(kBright);
        text(fCx, fCy - 6.0f * s, line, nullptr);
        if (cents == 0.0) std::snprintf(line, sizeof(line), "no shift");
        else std::snprintf(line, sizeof(line), "%+.3f semitones", cents / 100.0);
        fontSize(std::min(11.5f * s, fROct * 0.15f));
        fillColor(kText);
        text(fCx, fCy + 16.0f * s, line, nullptr);

        // In words, and what MIDI 1.0 makes of it, in the gap under the Divisions arc.
        const float ty = fCy + fR + 34.0f * s;
        std::string words = std::to_string(fN) + "-EDO: ";
        if (fOctaves != 0) words += signedCount(fOctaves, "octave") + ", ";
        words += signedCount(fSteps, "step");
        fontSize(12.5f * s);
        fillColor(fAccent);
        text(fCx, ty, words.c_str(), nullptr);

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
        text(fCx, ty + 18.0f * s, line, nullptr);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        if (!ev.press)
        {
            if (fDragging == kNone) return false;
            const int ring = fDragging;
            fDragging = kNone;
            fCallback->dialDragFinished(ring);
            return true;
        }
        const float px = static_cast<float>(ev.pos.getX()) - fCx, py = static_cast<float>(ev.pos.getY()) - fCy;
        const int   ring = ringAt(px, py);
        if (ring == kNone) return false;
        const bool twice = ring == fLastRing && ev.time - fLastPress < 350;
        fLastRing = ring;
        fLastPress = ev.time;
        fDragging = ring;
        fCallback->dialDragStarted(ring);
        if (twice) set(ring, ring == kDivisions ? 12 : 0);   // a double-click: the default
        else if (!onHandle(ring, px, py)) follow(ring, px, py, true);
        return true;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (fDragging == kNone) return false;
        follow(fDragging, static_cast<float>(ev.pos.getX()) - fCx, static_cast<float>(ev.pos.getY()) - fCy, false);
        return true;
    }

    // A step per notch, up (or right) for more; a trackpad's smooth scroll adds up to whole steps.
    bool onScroll(const ScrollEvent& ev) override
    {
        if (fDragging != kNone) return false;
        const int ring = ringAt(static_cast<float>(ev.pos.getX()) - fCx, static_cast<float>(ev.pos.getY()) - fCy);
        if (ring == kNone) return false;
        if (ring != fScrollRing) fScrollRing = ring, fScroll = 0.0;
        fScroll += ev.delta.getY() + ev.delta.getX();
        const int by = static_cast<int>(fScroll);
        if (by == 0) return true;
        fScroll -= by;
        fCallback->dialDragStarted(ring);
        set(ring, value(ring) + by);
        fCallback->dialDragFinished(ring);
        return true;
    }

private:
    // Steps' ring fits the view: Divisions' arc and its labels outside it, the words below it.
    void geometry()
    {
        const float s = fUiScale, w = getWidth(), h = getHeight(), pad = 8.0f * s, labels = 24.0f * s;
        const float below = 62.0f * s;
        fR    = std::max(20.0f, std::min((w * 0.5f - pad - labels) / 1.3f, (h - 2 * pad - labels - below) / 2.3f));
        fRDiv = fR * 1.3f;
        fROct = fR * 0.6f;
        fCx   = w * 0.5f;
        fCy   = pad + labels + fRDiv;
    }

    // --- the rings: angles and values ---

    static constexpr float kDivStart = 0.75f * views::kPi, kDivSweep = 1.5f * views::kPi;   // lower left, clockwise
    static float top(float turn) { return -0.5f * views::kPi + views::kTau * turn; }        // 0 at the top, clockwise
    float divAngle(int n) const { return kDivStart + kDivSweep * static_cast<float>(n - 1) / 95.0f; }
    float stepAngle(int k) const { return top(static_cast<float>(k) / static_cast<float>(fN)); }
    static constexpr float kOctStep = 0.1875f * views::kPi;   // 270° over the nine places, 0 at the top
    static float octAngle(int k) { return -0.5f * views::kPi + kOctStep * static_cast<float>(k); }

    int value(int ring) const { return ring == kDivisions ? fN : ring == kOctaves ? fOctaves : fSteps; }

    void set(int ring, int v)
    {
        if (ring == kDivisions) v = std::clamp(v, 1, 96);
        else if (ring == kOctaves) v = std::clamp(v, -4, 4);
        else v = std::clamp(v, -fN, fN);
        if (v == value(ring)) return;
        if (ring == kDivisions) fN = v, fSteps = std::clamp(fSteps, -fN, fN);
        else if (ring == kOctaves) fOctaves = v;
        else fSteps = v;
        fCallback->dialChanged(ring, v);
        repaint();
    }

    // The ring a point is on (relative to the centre): each owns the band halfway to its neighbours.
    int ringAt(float px, float py) const
    {
        const float d = std::hypot(px, py), s = fUiScale;
        if (d < fROct * 0.55f || d > fRDiv + 18.0f * s) return kNone;
        if (d < (fROct + fR) * 0.5f) return kOctaves;
        if (d < (fR + fRDiv) * 0.5f) return kSteps;
        return kDivisions;
    }

    bool onHandle(int ring, float px, float py) const
    {
        float hx, hy;
        handle(ring, hx, hy);
        return std::hypot(px - (hx - fCx), py - (hy - fCy)) < handleRadius() * 1.4f;
    }

    // The value at a point on a ring. On an arc, a point in its gap means an end: on a press the nearer
    // one, in a drag the one it went past. Steps goes round: of the places the angle could mean, the
    // one nearest the value now (so a drag carries on past the bottom), or on a fresh press, the
    // nearer way round from 0.
    void follow(int ring, float px, float py, bool fresh)
    {
        float turn = (std::atan2(py, px) + 0.5f * views::kPi) / views::kTau;   // 0 at the top, clockwise
        if (turn < 0.0f) turn += 1.0f;
        if (ring == kDivisions)
        {
            float t = std::atan2(py, px) - kDivStart;
            while (t < 0.0f) t += views::kTau;
            // In the gap: a press takes the nearer end, a drag stays at the end it went past.
            if (t > kDivSweep)
            {
                const bool high = fresh ? t - kDivSweep < (views::kTau - kDivSweep) * 0.5f : fN > 48;
                t = high ? kDivSweep : 0.0f;
            }
            set(ring, 1 + static_cast<int>(std::lround(t / kDivSweep * 95.0f)));
            return;
        }
        if (ring == kOctaves)
        {
            float from0 = std::atan2(py, px) + 0.5f * views::kPi;   // from the top, clockwise, −π … π
            if (from0 > views::kPi) from0 -= views::kTau;
            int k = static_cast<int>(std::lround(from0 / kOctStep));
            if (std::abs(k) > 4) k = fresh || fOctaves == 0 ? (k > 0 ? 4 : -4) : (fOctaves > 0 ? 4 : -4);
            set(ring, k);
            return;
        }
        const int places = fN;
        const int k      = static_cast<int>(std::lround(turn * static_cast<float>(places))) % places;
        int       best   = 2 * k <= places ? k : k - places;
        if (!fresh)
            for (int c : {k - places, k, k + places})
                if (std::abs(c - value(ring)) < std::abs(best - value(ring))) best = c;
        set(ring, best);
    }

    // --- drawing ---

    float handleRadius() const { return 11.0f * fUiScale; }

    void handle(int ring, float& x, float& y) const
    {
        const float a = ring == kDivisions ? divAngle(fN) : ring == kOctaves ? octAngle(fOctaves) : stepAngle(fSteps);
        const float r = ring == kDivisions ? fRDiv : ring == kOctaves ? fROct : fR;
        x = fCx + r * std::cos(a);
        y = fCy + r * std::sin(a);
    }

    void drawHandle(int ring, const char* label)
    {
        using namespace views;
        float x, y;
        handle(ring, x, y);
        const bool held = fDragging == ring;
        beginPath(); circle(x, y, handleRadius() * (held ? 1.12f : 1.0f));
        fillColor(lighten(fAccent, held ? 0.4f : 0.25f)); fill();
        strokeColor(Color(240, 244, 248)); strokeWidth(1.5f * fUiScale); stroke();
        if (!fHasFont) return;
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fitFont(label, 10.5f * fUiScale, handleRadius() * 1.8f);
        fillColor(Color(20, 24, 18));
        text(x, y + 0.5f, label, nullptr);
    }

    static std::string signedText(int v)
    {
        char text[16];
        std::snprintf(text, sizeof(text), v == 0 ? "0" : "%+d", v);
        return text;
    }

    void drawDivisions()
    {
        using namespace views;
        const float s = fUiScale;
        beginPath(); arc(fCx, fCy, fRDiv, kDivStart, kDivStart + kDivSweep, CW);
        strokeColor(kTrack); strokeWidth(5.0f * s); lineCap(ROUND); stroke();
        beginPath(); arc(fCx, fCy, fRDiv, kDivStart, divAngle(fN) + 0.001f, CW);
        strokeColor(withAlpha(fAccent, 0.55f)); strokeWidth(5.0f * s); stroke();
        lineCap(BUTT);
        // The common EDOs, marked and named.
        static const int kMarks[] = {1, 5, 12, 19, 22, 24, 31, 41, 53, 72, 96};
        for (int m : kMarks)
        {
            const float a = divAngle(m);
            beginPath();
            moveTo(fCx + (fRDiv + 5.0f * s) * std::cos(a), fCy + (fRDiv + 5.0f * s) * std::sin(a));
            lineTo(fCx + (fRDiv + 9.0f * s) * std::cos(a), fCy + (fRDiv + 9.0f * s) * std::sin(a));
            strokeColor(kDim); strokeWidth(1.2f * s); stroke();
            if (!fHasFont) continue;
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(9.5f * s);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(m == fN ? kText : kDim);
            const std::string t = std::to_string(m);
            text(fCx + (fRDiv + 17.0f * s) * std::cos(a), fCy + (fRDiv + 17.0f * s) * std::sin(a), t.c_str(), nullptr);
        }
        drawHandle(kDivisions, std::to_string(fN).c_str());
    }

    void drawSteps()
    {
        using namespace views;
        const float s = fUiScale;
        beginPath(); circle(fCx, fCy, fR); strokeColor(kTrack); strokeWidth(1.5f * s); stroke();
        for (int k = 0; k < 12; ++k)   // 12-TET's semitones, for comparison
        {
            const float a = top(static_cast<float>(k) / 12.0f);
            beginPath();
            moveTo(fCx + (fR + 6.0f * s) * std::cos(a), fCy + (fR + 6.0f * s) * std::sin(a));
            lineTo(fCx + (fR + 11.0f * s) * std::cos(a), fCy + (fR + 11.0f * s) * std::sin(a));
            strokeColor(Color(78, 83, 94)); strokeWidth(1.5f * s); stroke();
        }
        const float dot = std::clamp(fR * kPi / static_cast<float>(fN) * 0.32f, 1.4f * s, 4.5f * s);
        for (int k = 0; k < fN; ++k)
        {
            const float a = stepAngle(k);
            beginPath(); circle(fCx + fR * std::cos(a), fCy + fR * std::sin(a), k == 0 ? dot * 1.3f : dot);
            fillColor(k == 0 ? kText : kDim); fill();
        }
        if (fSteps != 0)
        {
            beginPath();
            if (std::abs(fSteps) == fN) circle(fCx, fCy, fR);
            else arc(fCx, fCy, fR, stepAngle(0), stepAngle(fSteps), fSteps > 0 ? CW : CCW);
            strokeColor(withAlpha(fAccent, 0.9f)); strokeWidth(4.0f * s); stroke();
        }
        drawHandle(kSteps, signedText(fSteps).c_str());
    }

    void drawOctaves()
    {
        using namespace views;
        const float s = fUiScale;
        beginPath(); arc(fCx, fCy, fROct, octAngle(-4), octAngle(4), CW);
        strokeColor(kTrack); strokeWidth(1.5f * s); stroke();
        for (int k = -4; k <= 4; ++k)
        {
            const float a = octAngle(k);
            beginPath(); circle(fCx + fROct * std::cos(a), fCy + fROct * std::sin(a), (k == 0 ? 3.6f : 2.8f) * s);
            fillColor(k == 0 ? kText : kDim); fill();
        }
        if (fOctaves != 0)
        {
            beginPath(); arc(fCx, fCy, fROct, octAngle(0), octAngle(fOctaves), fOctaves > 0 ? CW : CCW);
            strokeColor(withAlpha(fAccent, 0.9f)); strokeWidth(4.0f * s); stroke();
        }
        if (fHasFont)   // the ends named, as Divisions' are
            for (int k : {-4, 4})
            {
                const float a = octAngle(k);
                fontFace(NANOVG_DEJAVU_SANS_TTF);
                fontSize(9.5f * s);
                textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
                fillColor(k == fOctaves ? kText : kDim);
                text(fCx + (fROct - 15.0f * s) * std::cos(a), fCy + (fROct - 15.0f * s) * std::sin(a),
                     signedText(k).c_str(), nullptr);
            }
        drawHandle(kOctaves, signedText(fOctaves).c_str());
    }

    void fitFont(const char* text, float size, float width)
    {
        fontSize(size);
        Rectangle<float> r;
        textBounds(0, 0, text, nullptr, r);
        if (r.getWidth() > width && r.getWidth() > 0.0f) fontSize(std::max(size * 0.6f, size * width / r.getWidth()));
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
    int       fDragging = kNone;
    int       fLastRing = kNone, fScrollRing = kNone;
    uint      fLastPress = 0;
    double    fScroll = 0.0;
    float     fCx = 0, fCy = 0, fR = 10, fRDiv = 13, fROct = 6;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShiftDial)
};

END_NAMESPACE_DGL

#endif // RETUNE_VIEWS_HPP
