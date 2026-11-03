// RetuneUI.cpp — Retune's editor (docs/design.md, "Editor"), DPF + NanoVG, after NoteGran's.
//
// NoteGran's shell, cut down to one plugin: the header (the Unlikelyware logo and "Retune" in its
// accent), the house widget kit (Knob, GroupBox, Logo), and one fixed layout. On the left the shift
// on a circle of N steps (Views.hpp's ShiftCircle), its dot dragged or a step clicked setting Steps; on the right the
// Shift group (Divisions, Octaves, Steps) over what's held (HeldView: each note in, and where it
// went). The Steps knob turns through ±N, the range Retune uses, and shows the clamped value when a
// lower N has cut it (docs/design.md: the stored value is never rewritten).
//
// The editor reads every value on its timer and shows what changed, so host automation and a loaded
// state need no push. Edits go out through DPF's editParameter / setParameterValue, which the
// UIExporter hands to the seam (plugin_gui.h) as gestures and values.

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>   // the editor's own timer when the host has none
#endif
#include "DistrhoUI.hpp"

#include "src/DistrhoUIInternal.hpp"   // DPF's UIExporter (DPF_ROOT_DIR/distrho is on the include path)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "EmbeddedResources.hpp"   // build-time: the logo's bytes
#include "GroupBox.hpp"
#include "Knob.hpp"
#include "Logo.hpp"
#include "RetuneCore.h"
#include "Views.hpp"
#include "plugin_gui.h"

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::Color;
using DGL_NAMESPACE::GroupBox;
using DGL_NAMESPACE::HeldView;
using DGL_NAMESPACE::Knob;
using DGL_NAMESPACE::Logo;
using DGL_NAMESPACE::ShiftCircle;

namespace {

// --- the look ----------------------------------------------------------------------------------

// NoteGran's charcoal family; Retune's accent a leaf green, apart from NoteGran's five.
const Color kAccent(150, 200, 110);
const Color kWindow(26, 28, 33);

// The design size, in points; resizable at its ratio from 0.8×.
constexpr int kWidth = 760, kHeight = 480;

} // namespace

// --- the editor --------------------------------------------------------------------------------

class RetuneUI : public UI, public Knob::Callback, public ShiftCircle::Callback {
public:
    RetuneUI() : UI(kWidth, kHeight) {
        fPlugin = static_cast<const clap_plugin_t*>(getPluginInstancePointer());
        setGeometryConstraints(kWidth * 4 / 5, kHeight * 4 / 5, true, false);
        fHasFont = loadSharedResources();

        fLogo = std::make_unique<Logo>(this, EmbeddedResources::Unlikelyware_Final_ColorData,
                                       EmbeddedResources::Unlikelyware_Final_ColorDataSize);
        fCircle = std::make_unique<ShiftCircle>(this, this);
        fCircle->setAccent(kAccent);
        fHeld = std::make_unique<HeldView>(this);
        fHeld->setAccent(kAccent);

        fBox = std::make_unique<GroupBox>(this);
        fBox->setTitle("Shift");
        fBox->setAccentColor(kAccent);
        for (uint32_t id = 0; id < RT_PARAM_COUNT; ++id) {
            rt_param_info_t info{};
            if (fPlugin == nullptr || !rt_param_info(fPlugin, id, &info)) continue;
            Control& c = fControls[id];
            c.knob     = std::make_unique<Knob>(fBox.get(), this);
            c.knob->setId(id);
            c.knob->setLabel(info.name);
            c.knob->setAccentColor(kAccent);
            c.knob->configure(static_cast<float>(info.min), static_cast<float>(info.max), false,
                              static_cast<float>(info.def));
            c.knob->setInteger(true);
            c.knob->setFormatter([id](float v) { return format(id, static_cast<int>(std::lround(v))); });
            fBox->addControl(c.knob.get());
        }

        refresh(true);
        layout();
    }

    // A knob is a child of the box, and a DGL child unlinks itself from its parent as it goes: so
    // the knobs go first, then the box.
    ~RetuneUI() override {
        for (Control& c : fControls) c.knob.reset();
        fBox.reset();
    }

protected:
    // Values are read on the timer (uiIdle); this only keeps a pushed value from being stale.
    void parameterChanged(uint32_t index, float value) override {
        if (index < RT_PARAM_COUNT) show(index, value);
    }

    // --- controls → the plugin ---
    void knobDragStarted(Knob* k) override {
        fControls[k->getId()].dragging = true;
        editParameter(k->getId(), true);
    }
    void knobDragFinished(Knob* k) override {
        editParameter(k->getId(), false);
        fControls[k->getId()].dragging = false;
    }
    void knobValueChanged(Knob* k, float v) override {
        setParameterValue(k->getId(), v);
        fControls[k->getId()].shown = v;
        updateShift();
    }
    // The circle's dot, pressed and dragged: one gesture on Steps, as a knob's.
    void stepDragStarted() override {
        editParameter(RT_PARAM_STEPS, true);
        fControls[RT_PARAM_STEPS].circleDragging = true;
    }
    void stepChanged(int steps) override {
        setParameterValue(RT_PARAM_STEPS, static_cast<float>(steps));
        show(RT_PARAM_STEPS, static_cast<float>(steps));
    }
    void stepDragFinished() override {
        editParameter(RT_PARAM_STEPS, false);
        fControls[RT_PARAM_STEPS].circleDragging = false;
    }

    // The timer: values, and what's held.
    void uiIdle() override {
        if (!fInitialSizeNudged) {   // some Linux hosts apply the first size only once events run
            fInitialSizeNudged = true;
            setSize(getWidth(), getHeight());
        }
        refresh(false);
        rt_live_t live;
        if (fPlugin == nullptr || !rt_live_read(fPlugin, &live)) return;
        std::vector<HeldView::Note> in, out;
        for (uint32_t i = 0; i < live.inCount; ++i)
            in.push_back({live.inPitch[i] / 512.0f, live.inVelocity[i] / 65535.0f});
        for (uint32_t i = 0; i < live.outCount; ++i)
            out.push_back({live.outPitch[i] / 512.0f, live.outVelocity[i] / 65535.0f});
        if (!same(in, fIn) || !same(out, fOut)) {
            fIn  = in;
            fOut = out;
            fHeld->setHeld(std::move(in), std::move(out));
        }
    }

    void onNanoDisplay() override {
        const float w = getWidth(), h = getHeight();
        beginPath();
        rect(0, 0, w, h);
        fillColor(kWindow);
        fill();
        if (!fHasFont) return;
        const float s = scale();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fontSize(22 * s);
        fillColor(kAccent);
        text(fTitleX, 22 * s, "Retune", nullptr);
    }
    void onResize(const ResizeEvent& ev) override {
        UI::onResize(ev);
        layout();
    }

private:
    struct Control {
        std::unique_ptr<Knob> knob;
        bool                  dragging       = false;
        bool                  circleDragging = false;   // Steps, by the circle's dot
        float                 shown          = 0.0f;
    };

    float scale() const { return getWidth() / static_cast<float>(kWidth); }

    // A knob's readout: Steps as Retune uses it, cut to ±N.
    static std::string format(uint32_t id, int v) {
        char text[32];
        if (id == RT_PARAM_DIVISIONS) std::snprintf(text, sizeof(text), "%d-EDO", v);
        else if (v == 0) std::snprintf(text, sizeof(text), "0");
        else std::snprintf(text, sizeof(text), "%+d", v);
        return text;
    }

    int value(uint32_t id) const { return static_cast<int>(std::lround(fControls[id].shown)); }

    void show(uint32_t id, float v) {
        Control& c = fControls[id];
        c.shown    = v;
        if (c.knob) c.knob->setRealValue(v, false), c.knob->repaint();
        updateShift();
    }

    // Every value from the plugin; what changed (or everything) is shown. A knob being dragged keeps
    // what the drag says.
    void refresh(bool all) {
        if (fPlugin == nullptr) return;
        for (uint32_t i = 0; i < RT_PARAM_COUNT; ++i) {
            const float v = static_cast<float>(rt_param_value(fPlugin, i));
            if (fControls[i].dragging || fControls[i].circleDragging) continue;
            if (all || v != fControls[i].shown) show(i, v);
        }
    }

    // The Steps knob's range follows N, and the circle shows the shift Retune uses.
    void updateShift() {
        const retune::Params p = retune::clamped({value(RT_PARAM_DIVISIONS), value(RT_PARAM_OCTAVES), value(RT_PARAM_STEPS)});
        if (Knob* steps = fControls[RT_PARAM_STEPS].knob.get(); steps != nullptr && p.divisions != fStepsRange) {
            fStepsRange = p.divisions;
            steps->configure(static_cast<float>(-p.divisions), static_cast<float>(p.divisions), false, 0.0f);
        }
        if (Knob* steps = fControls[RT_PARAM_STEPS].knob.get(); steps != nullptr && !fControls[RT_PARAM_STEPS].dragging) {
            steps->setRealValue(static_cast<float>(p.steps), false);
            steps->repaint();
        }
        if (fCircle) fCircle->setShift(p.divisions, p.octaves, p.steps);
    }

    static bool same(const std::vector<HeldView::Note>& a, const std::vector<HeldView::Note>& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i].pitch != b[i].pitch || a[i].velocity != b[i].velocity) return false;
        return true;
    }

    // The circle down the left; the Shift group over what's held on the right.
    void layout() {
        const int   W = static_cast<int>(getWidth()), H = static_cast<int>(getHeight());
        const float s = scale();
        const int   M = static_cast<int>(14 * s), gap = static_cast<int>(10 * s), hdr = static_cast<int>(46 * s);

        fLogo->setHeightPreservingAspect(22 * s);
        fLogo->setAbsolutePos(M, static_cast<int>(11 * s));
        fTitleX = static_cast<float>(M + static_cast<int>(fLogo->getWidth()) + static_cast<int>(12 * s));

        const int leftW  = static_cast<int>(300 * s);
        const int bottom = H - M;
        fCircle->setUiScale(s);
        fCircle->setAbsolutePos(M, hdr);
        fCircle->setSize(static_cast<uint>(leftW), static_cast<uint>(bottom - hdr));

        const int rx = M + leftW + gap, rw = W - M - rx, boxH = static_cast<int>(150 * s);
        fBox->setUiScale(s);
        fBox->setAbsolutePos(rx, hdr);
        fBox->setSize(static_cast<uint>(rw), static_cast<uint>(boxH));
        fBox->relayout();
        fHeld->setUiScale(s);
        fHeld->setAbsolutePos(rx, hdr + boxH + gap);
        fHeld->setSize(static_cast<uint>(rw), static_cast<uint>(bottom - hdr - boxH - gap));

        // Knobs as large as their cells allow, to the design's size.
        for (Control& c : fControls)
            if (c.knob) {
                const float d = std::min({62.0f * s, static_cast<float>(c.knob->getHeight()) - 38.0f * s,
                                          static_cast<float>(c.knob->getWidth()) - 6.0f});
                c.knob->setDiameter(std::max(d, 16.0f), s);
            }
    }

    const clap_plugin_t*         fPlugin = nullptr;
    Control                      fControls[RT_PARAM_COUNT];
    std::unique_ptr<GroupBox>    fBox;
    std::unique_ptr<ShiftCircle> fCircle;
    std::unique_ptr<HeldView>    fHeld;
    std::unique_ptr<Logo>        fLogo;
    std::vector<HeldView::Note>  fIn, fOut;
    int                          fStepsRange        = 0;
    bool                         fHasFont           = false;
    bool                         fInitialSizeNudged = false;
    float                        fTitleX            = 16.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RetuneUI)
};

UI* createUI() { return new RetuneUI(); }

// ================================================================================================
// CLAP `gui` extension, as NoteGran's: gui_create() only makes a holder, since UIExporter can't be
// built until the host gives a parent window in set_parent().
// ================================================================================================

namespace {

struct GuiState {
    const clap_plugin_t*        plugin = nullptr;
    std::unique_ptr<UIExporter> exporter;
    uintptr_t                   parentWindow = 0;
    // The size in the host's units (points on macOS, pixels on X11): the source of truth, from
    // which the UIExporter's pixel size is derived at the live scale.
    uint32_t width           = kWidth;
    uint32_t height          = kHeight;
    double   appliedScale    = 0.0;
    bool     timerRegistered = false;
    clap_id  timerId         = 0;
#ifdef __APPLE__
    CFRunLoopTimerRef ownTimer = nullptr;   // when the host has no timer-support (startIdleTimer)
#endif
};

#ifdef __APPLE__
extern "C" double rt_gui_backing_scale(uintptr_t nsView);   // BackingScale_mac.mm
#endif

// Host units → pixels: on macOS the backing scale of the window the editor is in now (DPF caches
// its own once, maybe from the wrong screen); on X11 the sizes are pixels already.
double liveScale(const GuiState* gui) {
#ifdef __APPLE__
    return rt_gui_backing_scale(gui->parentWindow);
#else
    (void)gui;
    return 1.0;
#endif
}

void applySize(GuiState* gui) {
    if (!gui->exporter) return;
    const double scale = liveScale(gui);
    gui->appliedScale  = scale;
    gui->exporter->setWindowSizeFromHost(uint(std::lround(gui->width * scale)), uint(std::lround(gui->height * scale)));
}

#ifdef __APPLE__
constexpr const char* kWindowApi = CLAP_WINDOW_API_COCOA;
#else
constexpr const char* kWindowApi = CLAP_WINDOW_API_X11;
#endif

const clap_host_timer_support_t* hostTimer(const clap_plugin_t* plugin) {
    const clap_host_t* host = rt_plugin_host(plugin);
    return static_cast<const clap_host_timer_support_t*>(host->get_extension(host, CLAP_EXT_TIMER_SUPPORT));
}

// The editor's timer (rt_gui_on_timer) reads the values and what's held, and pumps DPF/pugl:
// without it nothing moves. It's the host's timer when the host has timer-support. Bitwig on macOS
// has none, so there the editor runs its own on the main run loop, in the common modes so it keeps
// firing during a drag, as NoteGran's does. Started at set_parent and at show, since not every host
// calls show for an embedded editor.
void startIdleTimer(const clap_plugin_t* plugin, GuiState* gui) {
    if (gui->timerRegistered) return;
    if (const auto* timer = hostTimer(plugin))
        if (timer->register_timer(rt_plugin_host(plugin), 16 /* ~60 fps */, &gui->timerId)) {
            gui->timerRegistered = true;
            return;
        }
#ifdef __APPLE__
    if (gui->ownTimer != nullptr) return;
    CFRunLoopTimerContext context = {0, gui, nullptr, nullptr, nullptr};
    gui->ownTimer = CFRunLoopTimerCreate(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + 0.016, 0.016, 0, 0,
                                         [](CFRunLoopTimerRef, void* info) {
                                             rt_gui_on_timer(static_cast<GuiState*>(info)->plugin, 0);
                                         },
                                         &context);
    if (gui->ownTimer != nullptr) CFRunLoopAddTimer(CFRunLoopGetMain(), gui->ownTimer, kCFRunLoopCommonModes);
#endif
}

void stopIdleTimer(const clap_plugin_t* plugin, GuiState* gui) {
    if (gui->timerRegistered) {
        if (const auto* timer = hostTimer(plugin)) timer->unregister_timer(rt_plugin_host(plugin), gui->timerId);
        gui->timerRegistered = false;
    }
#ifdef __APPLE__
    if (gui->ownTimer != nullptr) {
        CFRunLoopTimerInvalidate(gui->ownTimer);
        CFRelease(gui->ownTimer);
        gui->ownTimer = nullptr;
    }
#endif
}

bool gui_is_api_supported(const clap_plugin_t*, const char* api, bool isFloating) {
    return !isFloating && std::strcmp(api, kWindowApi) == 0;
}

bool gui_get_preferred_api(const clap_plugin_t*, const char** api, bool* isFloating) {
    *api        = kWindowApi;
    *isFloating = false;
    return true;
}

bool gui_create(const clap_plugin_t* plugin, const char* api, bool isFloating) {
    if (!gui_is_api_supported(plugin, api, isFloating)) return false;
    auto* gui   = new GuiState();
    gui->plugin = plugin;
    rt_plugin_set_gui(plugin, gui);
    return true;
}

void gui_destroy(const clap_plugin_t* plugin) {
    auto* gui = static_cast<GuiState*>(rt_plugin_get_gui(plugin));
    rt_plugin_set_gui(plugin, nullptr);
    if (gui == nullptr) return;
    stopIdleTimer(plugin, gui);
    delete gui;   // the UIExporter, and the editor with it
}

// The UIExporter's callbacks; `ptr` is the clap_plugin_t it was built with.
void onEditParam(void* ptr, uint32_t index, bool started) {
    const auto* plugin = static_cast<const clap_plugin_t*>(ptr);
    if (started) rt_param_begin_gesture(plugin, index);
    else rt_param_end_gesture(plugin, index);
}

void onSetParam(void* ptr, uint32_t index, float value) {
    rt_param_set_from_gui(static_cast<const clap_plugin_t*>(ptr), index, double(value));
}

bool gui_set_scale(const clap_plugin_t*, double) { return false; }   // cocoa's sizes are logical

bool gui_get_size(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) {
    auto* gui = static_cast<GuiState*>(rt_plugin_get_gui(plugin));
    if (gui == nullptr) return false;
    *width  = gui->width;
    *height = gui->height;
    return true;
}

bool gui_can_resize(const clap_plugin_t*) { return true; }

bool gui_get_resize_hints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) {
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically   = true;
    hints->preserve_aspect_ratio   = true;
    hints->aspect_ratio_width      = kWidth;
    hints->aspect_ratio_height     = kHeight;
    return true;
}

bool gui_adjust_size(const clap_plugin_t*, uint32_t* width, uint32_t* height) {
    // Keep the design's ratio and the smallest size (0.8×), as the UI's own constraints do.
    *width  = std::max<uint32_t>(*width, kWidth * 4 / 5);
    *height = static_cast<uint32_t>(std::lround(*width * double(kHeight) / kWidth));
    return true;
}

bool gui_set_size(const clap_plugin_t* plugin, uint32_t width, uint32_t height) {
    auto* gui = static_cast<GuiState*>(rt_plugin_get_gui(plugin));
    if (gui == nullptr) return false;
    gui->width  = width;
    gui->height = height;
    applySize(gui);
    return true;
}

bool gui_set_parent(const clap_plugin_t* plugin, const clap_window_t* window) {
    auto* gui = static_cast<GuiState*>(rt_plugin_get_gui(plugin));
    if (gui == nullptr || std::strcmp(window->api, kWindowApi) != 0) return false;
#ifdef __APPLE__
    gui->parentWindow = reinterpret_cast<uintptr_t>(window->cocoa);
#else
    gui->parentWindow = static_cast<uintptr_t>(window->x11);
#endif
    if (!gui->exporter) {
        gui->exporter.reset(new UIExporter(const_cast<clap_plugin_t*>(plugin), gui->parentWindow, 48000.0,
                                           onEditParam, onSetParam, nullptr, nullptr, nullptr, nullptr, nullptr,
                                           const_cast<clap_plugin_t*>(plugin), 0.0,
                                           DGL_NAMESPACE::Application::kTypeClassic));
        applySize(gui);
    }
    startIdleTimer(plugin, gui);
    return true;
}

bool gui_set_transient(const clap_plugin_t*, const clap_window_t*) { return false; }   // embedded only
void gui_suggest_title(const clap_plugin_t*, const char*) {}

bool gui_show(const clap_plugin_t* plugin) {
    auto* gui = static_cast<GuiState*>(rt_plugin_get_gui(plugin));
    if (gui == nullptr || !gui->exporter) return false;
    startIdleTimer(plugin, gui);
    return true;
}

bool gui_hide(const clap_plugin_t* plugin) {
    auto* gui = static_cast<GuiState*>(rt_plugin_get_gui(plugin));
    if (gui == nullptr || !gui->exporter) return false;
    stopIdleTimer(plugin, gui);
    return true;
}

const clap_plugin_gui_t kGui = {
    .is_api_supported  = gui_is_api_supported,
    .get_preferred_api = gui_get_preferred_api,
    .create            = gui_create,
    .destroy           = gui_destroy,
    .set_scale         = gui_set_scale,
    .get_size          = gui_get_size,
    .can_resize        = gui_can_resize,
    .get_resize_hints  = gui_get_resize_hints,
    .adjust_size       = gui_adjust_size,
    .set_size          = gui_set_size,
    .set_parent        = gui_set_parent,
    .set_transient     = gui_set_transient,
    .suggest_title     = gui_suggest_title,
    .show              = gui_show,
    .hide              = gui_hide,
};

} // namespace

extern "C" const clap_plugin_gui_t* rt_gui_extension(void) { return &kGui; }

// CLAP timer-support: re-derive the size if the window changed screens, then pump DPF/pugl.
extern "C" void rt_gui_on_timer(const clap_plugin_t* plugin, clap_id) {
    auto* gui = static_cast<GuiState*>(rt_plugin_get_gui(plugin));
    if (gui == nullptr || !gui->exporter) return;
    if (liveScale(gui) != gui->appliedScale) applySize(gui);
    gui->exporter->plugin_idle();
}

END_NAMESPACE_DISTRHO
