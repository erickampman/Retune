// HelpButton.hpp — a "?" box with a caption above it, styled like Dropdown so the two sit together
// in the header. A click opens the help page in a window of the plugin's own (uw-core's
// HelpWindow): the embedded help.md, drawn with NanoVG, with no browser, server or files.
// Copied from StruckSynth's src/UI (the house widget kit); Retune's changes are marked "Retune:".

#ifndef RETUNE_HELPBUTTON_HPP
#define RETUNE_HELPBUTTON_HPP

#include "NanoVG.hpp"
#include "uw/dgl/HelpWindow.hpp"   // uw-core: the help page in its own window

#include <string>
#include <vector>

START_NAMESPACE_DGL

class HelpButton : public NanoSubWidget
{
public:
    // The page (help.md), the images it shows by the names it uses, and where it opens: "#id", or
    // nullptr for the top.
    HelpButton(Widget* parent, std::string title, const uchar* page, uint pageSize,
               std::vector<uwhelp::Image> images, const char* anchor = nullptr)
        : NanoSubWidget(parent), fTitle(std::move(title)), fPage(page), fPageSize(pageSize),
          fImages(std::move(images)), fAnchor(anchor ? anchor : "")
    {
        fHasFont = loadSharedResources();
    }

    void setLabel(const std::string& label) { fLabel = label; }   // Retune: empty, the box alone
    void setUiScale(float s)
    {
        fUiScale = s;
        fBoxH   = static_cast<int>(26 * s);
        fLabelH = fLabel.empty() ? 0 : static_cast<int>(16 * s);   // Retune: no caption, no room for one
    }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth();
        const float by = boxTop(), bh = fBoxH, bw = bh, bx = (w - bw) * 0.5f;

        if (fHasFont && !fLabel.empty())
        {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            textAlign(ALIGN_CENTER | ALIGN_TOP);
            fontSize(13.0f * fUiScale);
            fillColor(Color(176, 182, 190));
            text(w * 0.5f, by - fLabelH + 1.0f, fLabel.c_str(), nullptr);
        }

        beginPath(); roundedRect(bx, by, bw, bh, 4.0f);
        fillColor(fPressed ? Color(48, 52, 60) : Color(34, 37, 43)); fill();
        strokeColor(Color(70, 76, 86)); strokeWidth(1.0f); stroke();

        if (fHasFont)
        {
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fontSize(15.0f * fUiScale);
            fillColor(Color(226, 230, 236));
            text(bx + bw * 0.5f, by + bh * 0.5f, "?", nullptr);
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        // DPF offers every sibling this event regardless of position — only claim our own area.
        if (ev.button != 1)
            return false;
        if (!ev.press)
        {
            if (!fPressed)
                return false;
            fPressed = false;
            repaint();
            return true;
        }
        if (ev.pos.getX() < 0 || ev.pos.getX() >= getWidth()
            || ev.pos.getY() < 0 || ev.pos.getY() >= getHeight())
            return false;
        fPressed = true;
        repaint();
        openHelp();
        return true;
    }

private:
    float boxTop() const
    {
        const float rem = getHeight() - (fLabelH + fBoxH);
        return (rem > 0.0f ? rem * 0.5f : 0.0f) + fLabelH;
    }

    void openHelp()
    {
        fHelp.open(*this, fTitle.c_str(), fPage, fPageSize, fImages, fAnchor.empty() ? nullptr : fAnchor.c_str());
    }

    std::string                 fTitle;
    const uchar*                fPage     = nullptr;
    uint                        fPageSize = 0;
    std::vector<uwhelp::Image>  fImages;
    std::string                 fAnchor;
    uwhelp::HelpOpener          fHelp;   // the help window, while the editor is open
    std::string       fLabel;
    bool              fHasFont = false;
    bool              fPressed = false;
    float             fUiScale = 1.0f;

    int fBoxH = 26, fLabelH = 16;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HelpButton)
};

END_NAMESPACE_DGL

#endif // RETUNE_HELPBUTTON_HPP
