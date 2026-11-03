// Knob.hpp — a reusable labelled rotary knob for DPF NanoVG UIs.
//
// It is a self-contained NanoSubWidget: give it a bounding box, a range, a caption and a value
// formatter, and it draws itself (arc value-ring + bevelled body + indicator + label + live
// readout) and handles its own interaction.
//
// Interaction comes from DPF's KnobEventHandler (vertical drag, scroll-wheel, double-click-to-
// default), but the knob always drives that handler in a *linear normalized* [0,1] space and
// applies any log law itself. That is deliberate: DPF's KnobEventHandler log-scale drag is
// broken (it re-applies logscale() to its linear accumulator every motion event, pinning wide
// ranges near the minimum), and a normalized handler also gives every knob — linear or log —
// the same predictable "one drag = full range" feel.
//
// Usage:
//   auto k = std::make_unique<Knob>(this, this);  // parent widget, Knob::Callback*
//   k->setId(paramIndex);                          // so the callback knows which control fired
//   k->setLabel("Attack");
//   k->configure(0.001f, 10.0f, /*logarithmic=*/true, /*def=*/0.01f);
//   k->setFormatter([](float v){ ... return std::string; });
//   k->setRealValue(0.01f);
//   k->setSize(w, h); k->setAbsolutePos(x, y);
// Copied from NoteGran's src/UI, itself from StruckSynth's (the house widget kit); NoteGran's changes
// are marked "NoteGran:", Retune's "Retune:".

#ifndef RETUNE_KNOB_HPP
#define RETUNE_KNOB_HPP

#include "NanoVG.hpp"
#include "EventHandlers.hpp"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

START_NAMESPACE_DGL

class Knob : public NanoSubWidget,
             public KnobEventHandler,
             private KnobEventHandler::Callback
{
public:
    // A knob reports in real (mapped) units and identifies itself by Widget id.
    class Callback
    {
    public:
        virtual ~Callback() {}
        virtual void knobDragStarted(Knob* knob) = 0;
        virtual void knobDragFinished(Knob* knob) = 0;
        virtual void knobValueChanged(Knob* knob, float realValue) = 0;
    };

    explicit Knob(Widget* parent, Callback* callback)
        : NanoSubWidget(parent),
          KnobEventHandler(this),
          fCallback(callback)
    {
        fHasFont = loadSharedResources(); // DPF's bundled DejaVu Sans for this NanoVG context
        KnobEventHandler::setRange(0.0f, 1.0f);   // the handler works in normalized space
        KnobEventHandler::setDefault(0.0f);
        setOrientation(KnobEventHandler::Vertical); // drag up = increase
        KnobEventHandler::setCallback(this);        // we intercept + remap to real units
    }

    // Real-unit range + optional log law + default (for double-click reset).
    void configure(float min, float max, bool logarithmic, float def)
    {
        fMin = min;
        fMax = max;
        fLog = logarithmic;
        KnobEventHandler::setDefault(toNormalized(def));
    }

    void setRealValue(float v, bool sendCallback = false)
    {
        KnobEventHandler::setValue(toNormalized(v), sendCallback);
    }
    float getRealValue() const { return toReal(KnobEventHandler::getValue()); }

    void setLabel(const std::string& label) { fLabel = label; }
    // NoteGran: a stepped parameter's knob moves in whole steps.
    void setInteger(bool integer) { fInteger = integer; }
    void setAccentColor(Color color)         { fAccent = color; }
    void setFormatter(std::function<std::string(float)> fn) { fFormat = std::move(fn); }

    // A disabled knob is greyed out and ignores all interaction (the value is still shown).
    void setEnabled(bool e) { if (fEnabled != e) { fEnabled = e; repaint(); } }
    bool isEnabled() const  { return fEnabled; }

    // Fixed sphere diameter (px) so every knob renders the same size regardless of its cell.
    // 0 = auto-fit to the box. A matching text scale keeps label/readout proportional.
    void setDiameter(float d, float textScale = 1.0f)
    {
        fDiameter = d;
        fTextScale = textScale < 0.6f ? 0.6f : textScale;
        repaint();
    }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth();
        const float h = getHeight();

        // A compact label / sphere / value unit, centred vertically so text hugs the sphere even
        // in a tall cell (PM style: caption on top, value box on the bottom).
        const float labelBand = 16.0f * fTextScale;
        const float valueBand = 18.0f * fTextScale;
        const float cx        = w * 0.5f;

        // Uniform diameter when set; otherwise fit. Never overflow the cell.
        float d = fDiameter > 0.0f ? fDiameter : (h - labelBand - valueBand);
        d = std::fmin(d, std::fmin(w, h - labelBand - valueBand));
        if (d < 0.0f) d = 0.0f;
        const float r     = d * 0.5f;
        const float unitH = labelBand + d + valueBand;
        const float top   = std::fmax(0.0f, (h - unitH) * 0.5f);
        const float cy    = top + labelBand + r;

        const float frac     = getNormalizedValue();
        const float valAngle = kArcStart + frac * kArcSweep;

        const Color base = fEnabled ? fAccent : Color(104, 108, 116);
        const float I    = fEnabled ? 1.0f : 0.45f; // dim when disabled

        // Outer glow: bright at the centre, fading to transparent at the rim (blends into panel).
        Paint glow = radialGradient(cx, cy, r * 0.15f, r,
                                    withA(base, 0.80f * I), withA(base, 0.0f));
        beginPath(); circle(cx, cy, r); fillPaint(glow); fill();

        // Brighter core.
        Paint core = radialGradient(cx, cy, 0.0f, r * 0.62f,
                                    withA(lighten(base, 0.35f), 0.85f * I), withA(base, 0.0f));
        beginPath(); circle(cx, cy, r * 0.64f); fillPaint(core); fill();

        // Faint rim + a soft top-left glint (gradient, not a hard disc, so it reads subtly).
        beginPath(); circle(cx, cy, r);
        strokeColor(withA(Color(255, 255, 255), 0.14f * I)); strokeWidth(1.2f); stroke();
        const float hx = cx - r * 0.30f, hy = cy - r * 0.34f, hr = r * 0.26f;
        Paint glint = radialGradient(hx, hy, 0.0f, hr,
                                     withA(Color(255, 255, 255), 0.11f * I),
                                     withA(Color(255, 255, 255), 0.0f));
        beginPath(); circle(hx, hy, hr); fillPaint(glint); fill();

        // Position indicator: a bright dot near the rim at the value angle.
        const float ix = cx + std::cos(valAngle) * (r * 0.72f);
        const float iy = cy + std::sin(valAngle) * (r * 0.72f);
        beginPath(); circle(ix, iy, std::fmax(2.0f, r * 0.13f));
        fillColor(fEnabled ? Color(244, 247, 250) : Color(150, 154, 160)); fill();

        if (fHasFont)
        {
            const float real = getRealValue();
            fontFace(NANOVG_DEJAVU_SANS_TTF);

            // Label on top, tinted toward the module accent.
            textAlign(ALIGN_CENTER | ALIGN_TOP);
            // NoteGran: long labels shrink, leaving a gap to the next knob's (cells abut)
            fitFont(fLabel.c_str(), 12.0f * fTextScale, w - 8.0f * fTextScale);
            fillColor(fEnabled ? lighten(fAccent, 0.45f) : Color(112, 117, 124));
            text(cx, top, fLabel.c_str(), nullptr);

            // Value readout in a subtle box just below the sphere.
            const std::string readout = fFormat ? fFormat(real) : defaultFormat(real);
            const float bh = valueBand - 2.0f;
            const float bw = std::fmin(w - 4.0f, 64.0f * fTextScale);
            const float bx = cx - bw * 0.5f, by = top + labelBand + d + 1.0f;
            beginPath(); roundedRect(bx, by, bw, bh, 3.0f);
            fillColor(Color(20, 22, 27)); fill();
            strokeColor(Color(46, 50, 58)); strokeWidth(1.0f); stroke();
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fitFont(readout.c_str(), 11.5f * fTextScale, bw - 4.0f);
            fillColor(fEnabled ? Color(224, 228, 234) : Color(120, 125, 132));
            text(cx, by + bh * 0.5f, readout.c_str(), nullptr);
        }
    }

    // Hand interaction to the KnobEventHandler (a disabled knob ignores all of it).
    bool onMouse(const MouseEvent& ev)   override { return fEnabled && mouseEvent(ev); }
    bool onMotion(const MotionEvent& ev) override { return fEnabled && motionEvent(ev); }
    bool onScroll(const ScrollEvent& ev) override { return fEnabled && scrollEvent(ev); }

private:
    // KnobEventHandler::Callback — remap normalized → real and forward to the user callback.
    void knobDragStarted(SubWidget*) override  { fCallback->knobDragStarted(this); }
    void knobDragFinished(SubWidget*) override { fCallback->knobDragFinished(this); }
    void knobValueChanged(SubWidget*, float norm) override
    {
        fCallback->knobValueChanged(this, toReal(norm));
        repaint();
    }

    float toReal(float norm) const
    {
        norm = norm < 0.0f ? 0.0f : (norm > 1.0f ? 1.0f : norm);
        const float v = fLog ? fMin * std::pow(fMax / fMin, norm) : fMin + norm * (fMax - fMin);
        return fInteger ? std::round(v) : v;
    }
    float toNormalized(float v) const
    {
        v = v < fMin ? fMin : (v > fMax ? fMax : v);
        return fLog ? std::log(v / fMin) / std::log(fMax / fMin) : (v - fMin) / (fMax - fMin);
    }

    Callback*                         fCallback;
    std::string                       fLabel;
    std::function<std::string(float)> fFormat;
    Color                             fAccent { 79, 208, 224 };
    bool                              fHasFont = false;
    bool                              fEnabled = true;
    float                             fDiameter = 0.0f;   // 0 = auto-fit
    float                             fTextScale = 1.0f;
    float                             fMin = 0.0f, fMax = 1.0f;
    bool                              fLog = false;
    bool                              fInteger = false;

    // NoteGran: the font size, at most `size`, that fits `text` in `width`.
    void fitFont(const char* text, float size, float width)
    {
        fontSize(size);
        Rectangle<float> r;
        textBounds(0, 0, text, nullptr, r);
        if (r.getWidth() > width && r.getWidth() > 0.0f)
            fontSize(std::fmax(size * 0.6f, size * width / r.getWidth()));
    }

    // Colour helpers (DPF Color components are floats in [0,1]).
    static Color withA(Color c, float a) { c.alpha = a; return c; }
    static Color lighten(Color c, float f)
    {
        c.red   += (1.0f - c.red)   * f;
        c.green += (1.0f - c.green) * f;
        c.blue  += (1.0f - c.blue)  * f;
        return c;
    }

    static constexpr float kArcStart = 0.75f * 3.14159265358979f; // 135°
    static constexpr float kArcSweep = 1.50f * 3.14159265358979f; // 270° sweep, gap at bottom

    static std::string defaultFormat(float v)
    {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%.2f", v);
        return buf;
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Knob)
};

END_NAMESPACE_DGL

#endif // RETUNE_KNOB_HPP
