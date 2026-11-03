// GroupBox.hpp — a reusable titled panel that groups related controls for DPF NanoVG UIs.
//
// It draws a rounded panel with a caption (e.g. "ADSR Envelope") and acts as a container:
// child controls are parented to it, and it lays them out in a row inside its content area
// (below the title strip, within padding). As the synth grows, each module becomes one
// GroupBox — make a box, parent its knobs to it, addControl() each, position + size the box,
// call relayout().
//
// Positions are window-absolute in DGL (a subwidget's coords are not offset by its parent),
// so relayout() places children relative to the box's own absolute position. Mouse events
// still reach the nested controls: the default Widget::onMouse forwards to subwidgets, and
// this box does not override it.
// Copied from NoteGran's src/UI, itself from StruckSynth's (the house widget kit); NoteGran's changes
// are marked "NoteGran:", Retune's "Retune:".

#ifndef RETUNE_GROUPBOX_HPP
#define RETUNE_GROUPBOX_HPP

#include "NanoVG.hpp"

#include <algorithm>
#include <string>
#include <vector>

START_NAMESPACE_DGL

class GroupBox : public NanoSubWidget
{
public:
    explicit GroupBox(Widget* parent)
        : NanoSubWidget(parent)
    {
        fHasFont = loadSharedResources(); // DPF's bundled DejaVu Sans
    }

    void setTitle(const std::string& title) { fTitle = title; }
    void setAccentColor(Color color)        { fAccent = color; }
    // Scales font size AND the title-strip/padding geometry together — scaling font alone
    // (without the strip it sits in) is what causes titles to overflow into the content area.
    void setUiScale(float s)
    {
        fUiScale     = s;
        fPadding     = static_cast<int>(10 * s);
        fTitleHeight = static_cast<int>(28 * s);
    }

    // Register a control (already parented to this box) for row layout. NoteGran: a control may
    // take several cells (a wide menu), and the box may wrap into rows of `columns` cells.
    void addControl(SubWidget* control, int cells = 1)
    {
        fControls.push_back(control);
        fCells.push_back(cells < 1 ? 1 : cells);
    }
    int  controlCount() const { return static_cast<int>(fControls.size()); }
    void setColumns(int columns) { fColumns = columns; }   // NoteGran: 0 is one row
    // NoteGran: the box's width in cells (its columns, or all its cells in one row).
    int  widthInCells() const
    {
        int total = 0;
        for (int c : fCells) total += c;
        return fColumns > 0 ? fColumns : (total > 0 ? total : 1);
    }

    // The inner content rectangle (window-absolute), below the title strip and inside padding.
    // For panels that lay their children out themselves rather than via relayout().
    void contentBounds(int& x, int& y, int& w, int& h) const
    {
        x = getAbsoluteX() + fPadding;
        y = getAbsoluteY() + fTitleHeight;
        w = static_cast<int>(getWidth())  - 2 * fPadding;
        h = static_cast<int>(getHeight()) - fTitleHeight - fPadding;
    }

    // Lay the registered controls out evenly across the content area. NoteGran: in rows of
    // widthInCells() cells, a control that doesn't fit the rest of a row starting the next.
    void relayout()
    {
        const int n = static_cast<int>(fControls.size());
        if (n == 0)
            return;

        const int contentX = getAbsoluteX() + fPadding;
        const int contentY = getAbsoluteY() + fTitleHeight;
        const int contentW = static_cast<int>(getWidth())  - 2 * fPadding;
        const int contentH = static_cast<int>(getHeight()) - fTitleHeight - fPadding;
        if (contentW <= 0 || contentH <= 0)
            return;

        const int cols = widthInCells();
        std::vector<int> row(fControls.size()), col(fControls.size());
        int r = 0, c = 0;
        for (int i = 0; i < n; ++i)
        {
            if (c > 0 && c + fCells[i] > cols) { ++r; c = 0; }
            row[i] = r;
            col[i] = c;
            c += fCells[i];
        }
        const int rows = r + 1;
        for (int i = 0; i < n; ++i)
        {
            const int x0 = contentW * col[i] / cols, x1 = contentW * std::min(col[i] + fCells[i], cols) / cols;
            const int y0 = contentH * row[i] / rows, y1 = contentH * (row[i] + 1) / rows;
            fControls[i]->setSize(static_cast<uint>(x1 - x0), static_cast<uint>(y1 - y0));
            fControls[i]->setAbsolutePos(contentX + x0, contentY + y0);
        }
    }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth();
        const float h = getHeight();

        // panel fill (slightly raised from the window background)
        beginPath();
        roundedRect(1.5f, 1.5f, w - 3.0f, h - 3.0f, 8.0f);
        fillColor(Color(40, 43, 50));   // NoteGran: charcoal, not StruckSynth's brown
        fill();

        // border
        beginPath();
        roundedRect(1.5f, 1.5f, w - 3.0f, h - 3.0f, 8.0f);
        strokeColor(Color(62, 67, 77));
        strokeWidth(1.5f);
        stroke();

        // title
        if (fHasFont)
        {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fontSize(13.0f * fUiScale);
            fillColor(fAccent);
            text(12.0f * fUiScale, fTitleHeight * 0.5f + 3.0f, fTitle.c_str(), nullptr);
        }
    }

private:
    std::vector<SubWidget*> fControls;
    std::vector<int>        fCells;
    int                     fColumns = 0;
    std::string             fTitle;
    Color                   fAccent { 150, 200, 210 };
    bool                    fHasFont = false;
    float                   fUiScale = 1.0f;

    int fPadding     = 10;
    int fTitleHeight = 28;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GroupBox)
};

END_NAMESPACE_DGL

#endif // RETUNE_GROUPBOX_HPP
