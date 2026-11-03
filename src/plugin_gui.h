// plugin_gui.h — the seam between the plugin (src/plugin.cpp, the raw CLAP plugin) and its editor
// (src/UI/RetuneUI.cpp, DPF + NanoVG), as NoteGran's and StruckSynth's. Neither side includes the
// other's internals; they meet only at these C-linkage functions.
//
// Provided by plugin.cpp, called by the editor:
//   rt_plugin_*   per-instance plumbing
//   rt_param_*    a parameter's value, and edits with gestures
// Provided by the editor, called by plugin.cpp:
//   rt_gui_extension   the CLAP gui vtable for this platform
//   rt_gui_on_timer    CLAP timer-support: pumps the DPF/pugl event loop and refreshes the editor
//
// Host automation needs no push: the editor reads every value on its timer and shows what changed.

#pragma once

#include <clap/clap.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- provided by plugin.cpp, called by the editor --------------------------------------------

const clap_host_t* rt_plugin_host(const clap_plugin_t* plugin);

// The editor owns its per-instance state; plugin.cpp only holds the pointer for it.
void  rt_plugin_set_gui(const clap_plugin_t* plugin, void* gui);
void* rt_plugin_get_gui(const clap_plugin_t* plugin);

// The parameters' ids (plugin.cpp's kParams): appended, never moved. All are whole numbers: the first
// three's ranges are RetuneCore.h's, Dry is 0 (Off) or 1 (On).
enum { RT_PARAM_DIVISIONS = 0, RT_PARAM_OCTAVES = 1, RT_PARAM_STEPS = 2, RT_PARAM_DRY = 3, RT_PARAM_COUNT = 4 };

double rt_param_value(const clap_plugin_t* plugin, uint32_t index);

// A drag is begin_gesture, set_from_gui (repeatedly), end_gesture. plugin.cpp applies each value at
// its next process() (or flush) and tells the host, so the move is recorded as automation.
void rt_param_begin_gesture(const clap_plugin_t* plugin, uint32_t index);
void rt_param_set_from_gui(const clap_plugin_t* plugin, uint32_t index, double value);
void rt_param_end_gesture(const clap_plugin_t* plugin, uint32_t index);

// --- provided by the editor, called by plugin.cpp --------------------------------------------
const clap_plugin_gui_t* rt_gui_extension(void);
void                     rt_gui_on_timer(const clap_plugin_t* plugin, clap_id timer_id);

#ifdef __cplusplus
}
#endif
