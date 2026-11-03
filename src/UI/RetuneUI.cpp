// RetuneUI.cpp — Retune's editor (docs/design.md, "Editor"), DPF + NanoVG, after NoteGran's.
//
// NoteGran's shell, cut down to one plugin: the header (the Unlikelyware logo and "Retune" in its
// accent) and one view, the dial (Views.hpp's ShiftDial): Divisions as an arc outside, Steps the
// ring inside it, Octaves the ring inside that, each dragged round by its handle; Dry a checkbox in
// the header. Steps turns
// through ±N, the range Retune uses, and shows the clamped value when a lower N has cut the stored
// one (docs/design.md: the stored value is never rewritten).
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
#include <cstring>
#include <memory>

#include "EmbeddedResources.hpp"   // build-time: the logo's bytes
#include "Logo.hpp"
#include "RetuneCore.h"
#include "Toggle.hpp"
#include "Views.hpp"
#include "plugin_gui.h"

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::Color;
using DGL_NAMESPACE::Logo;
using DGL_NAMESPACE::ShiftDial;
using DGL_NAMESPACE::Toggle;

namespace {

// --- the look ----------------------------------------------------------------------------------

// NoteGran's charcoal family; Retune's accent a leaf green, apart from NoteGran's five.
const Color kAccent(150, 200, 110);
const Color kWindow(26, 28, 33);

// The design size, in points; resizable at its ratio from 0.8×.
constexpr int kWidth = 520, kHeight = 560;

} // namespace

// --- the editor --------------------------------------------------------------------------------

class RetuneUI : public UI, public ShiftDial::Callback, public Toggle::Callback {
public:
    RetuneUI() : UI(kWidth, kHeight) {
        fPlugin = static_cast<const clap_plugin_t*>(getPluginInstancePointer());
        setGeometryConstraints(kWidth * 4 / 5, kHeight * 4 / 5, true, false);
        fHasFont = loadSharedResources();

        fLogo = std::make_unique<Logo>(this, EmbeddedResources::Unlikelyware_Final_ColorData,
                                       EmbeddedResources::Unlikelyware_Final_ColorDataSize);
        fDial = std::make_unique<ShiftDial>(this, this);
        fDial->setAccent(kAccent);
        fDry = std::make_unique<Toggle>(this, this);
        fDry->setId(RT_PARAM_DRY);
        fDry->setLabel("Dry: the originals too");
        fDry->setLabelBeside(true);
        fDry->setAccentColor(kAccent);

        refresh(true);
        layout();
    }

protected:
    // Values are read on the timer (uiIdle); this only keeps a pushed value from being stale.
    void parameterChanged(uint32_t index, float value) override {
        if (index < RT_PARAM_COUNT) show(index, value);
    }

    // --- the dial → the plugin: a press, drag and release is one gesture, as a knob's ---
    void dialDragStarted(int ring) override {
        fDragging[ring] = true;
        editParameter(static_cast<uint32_t>(ring), true);
    }
    void dialChanged(int ring, int value) override {
        setParameterValue(static_cast<uint32_t>(ring), static_cast<float>(value));
        fShown[ring] = static_cast<float>(value);
        updateDial();
    }
    void dialDragFinished(int ring) override {
        editParameter(static_cast<uint32_t>(ring), false);
        fDragging[ring] = false;
    }

    void toggleChanged(Toggle*, bool on) override {
        editParameter(RT_PARAM_DRY, true);
        setParameterValue(RT_PARAM_DRY, on ? 1.0f : 0.0f);
        editParameter(RT_PARAM_DRY, false);
        fShown[RT_PARAM_DRY] = on ? 1.0f : 0.0f;
    }

    // The timer: the values.
    void uiIdle() override {
        if (!fInitialSizeNudged) {   // some Linux hosts apply the first size only once events run
            fInitialSizeNudged = true;
            setSize(getWidth(), getHeight());
        }
        refresh(false);
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
    float scale() const { return getWidth() / static_cast<float>(kWidth); }

    int value(uint32_t id) const { return static_cast<int>(std::lround(fShown[id])); }

    void show(uint32_t id, float v) {
        fShown[id] = v;
        if (id == RT_PARAM_DRY) fDry->setState(v >= 0.5f);
        else updateDial();
    }

    // Every value from the plugin; what changed (or everything) is shown. A ring being dragged keeps
    // what the drag says.
    void refresh(bool all) {
        if (fPlugin == nullptr) return;
        for (uint32_t i = 0; i < RT_PARAM_COUNT; ++i) {
            const float v = static_cast<float>(rt_param_value(fPlugin, i));
            if (fDragging[i]) continue;
            if (all || v != fShown[i]) show(i, v);
        }
    }

    // The dial shows the shift Retune uses: Steps cut to ±N.
    void updateDial() {
        const retune::Params p = retune::clamped({value(RT_PARAM_DIVISIONS), value(RT_PARAM_OCTAVES), value(RT_PARAM_STEPS)});
        if (fDial) fDial->setShift(p.divisions, p.octaves, p.steps);
    }

    void layout() {
        const int   W = static_cast<int>(getWidth()), H = static_cast<int>(getHeight());
        const float s = scale();
        const int   M = static_cast<int>(14 * s), hdr = static_cast<int>(46 * s);

        fLogo->setHeightPreservingAspect(22 * s);
        fLogo->setAbsolutePos(M, static_cast<int>(11 * s));
        fTitleX = static_cast<float>(M + static_cast<int>(fLogo->getWidth()) + static_cast<int>(12 * s));

        // Dry at the header's right.
        const int dryW = static_cast<int>(170 * s);
        fDry->setUiScale(s);
        fDry->setAbsolutePos(W - M - dryW, static_cast<int>(10 * s));
        fDry->setSize(static_cast<uint>(dryW), static_cast<uint>(24 * s));

        fDial->setUiScale(s);
        fDial->setAbsolutePos(M, hdr);
        fDial->setSize(static_cast<uint>(W - 2 * M), static_cast<uint>(H - hdr - M));
    }

    const clap_plugin_t*       fPlugin = nullptr;
    float                      fShown[RT_PARAM_COUNT]    = {};
    bool                       fDragging[RT_PARAM_COUNT] = {};
    std::unique_ptr<ShiftDial> fDial;
    std::unique_ptr<Toggle>    fDry;
    std::unique_ptr<Logo>      fLogo;
    bool                       fHasFont           = false;
    bool                       fInitialSizeNudged = false;
    float                      fTitleX            = 16.0f;

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
