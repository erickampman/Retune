// Toggle.hpp — a reusable labelled on/off checkbox for DPF NanoVG UIs. Bind it to a boolean
// param with Widget::setId(); clicking flips it and reports via the Callback. Like the other
// custom widgets it bounds-checks onMouse (DPF offers the event to every sibling).
// Copied from NoteGran's src/UI, itself from StruckSynth's (the house widget kit); NoteGran's changes
// are marked "NoteGran:", Retune's "Retune:".

#ifndef RETUNE_TOGGLE_HPP
#define RETUNE_TOGGLE_HPP

#include "NanoVG.hpp"

#include <string>

START_NAMESPACE_DGL

class Toggle : public NanoSubWidget
{
public:
    class Callback
    {
    public:
        virtual ~Callback() {}
        virtual void toggleChanged(Toggle* toggle, bool on) = 0;
    };

    explicit Toggle(Widget* parent, Callback* callback)
        : NanoSubWidget(parent), fCallback(callback)
    {
        fHasFont = loadSharedResources();
    }

    void setLabel(const std::string& label) { fLabel = label; }
    void setState(bool on)                  { fOn = on; repaint(); }
    void setAccentColor(Color c)            { fAccent = c; }   // NoteGran: the plugin's colour
    void setUiScale(float s)                { fUiScale = s; }
    // Retune: the label to the right of the box, on one line (for a header), not a caption above it.
    void setLabelBeside(bool beside)        { fBeside = beside; }
    bool state() const { return fOn; }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth(), h = getHeight();
        const float box = fBeside ? 18.0f * fUiScale : 22.0f;   // Retune: scaled beside a label
        const float bx = fBeside ? 0.0f : (w - box) * 0.5f;
        const float by = fBeside ? (h - box) * 0.5f : (h - box) * 0.5f + 8.0f; // leave room for the caption above

        if (fHasFont && fBeside)
        {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fontSize(13.0f * fUiScale);
            fillColor(Color(176, 182, 190));
            text(box + 8.0f * fUiScale, h * 0.5f, fLabel.c_str(), nullptr);
        }
        else if (fHasFont)
        {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
            fontSize(13.0f * fUiScale);
            fillColor(Color(176, 182, 190));
            text(w * 0.5f, by - 6.0f, fLabel.c_str(), nullptr);
        }

        beginPath(); roundedRect(bx, by, box, box, 4.0f);
        fillColor(fOn ? fAccent : Color(34, 37, 43));
        fill();
        strokeColor(Color(70, 76, 86)); strokeWidth(1.0f); stroke();

        if (fOn)
        {
            beginPath();
            const float u = box / 22.0f;   // Retune: the tick scales with the box
            moveTo(bx + 5 * u, by + 11 * u); lineTo(bx + 9 * u, by + 16 * u); lineTo(bx + 17 * u, by + 6 * u);
            strokeColor(Color(14, 22, 26)); strokeWidth(2.0f * u); stroke();
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1 || !ev.press)
            return false;
        if (ev.pos.getX() < 0 || ev.pos.getX() >= getWidth()
            || ev.pos.getY() < 0 || ev.pos.getY() >= getHeight())
            return false;
        fOn = !fOn;
        repaint();
        fCallback->toggleChanged(this, fOn);
        return true;
    }

private:
    Callback*   fCallback;
    Color       fAccent { 63, 185, 207 };
    std::string fLabel;
    bool        fOn = false;
    bool        fHasFont = false;
    bool        fBeside  = false;
    float       fUiScale = 1.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Toggle)
};

END_NAMESPACE_DGL

#endif // RETUNE_TOGGLE_HPP
