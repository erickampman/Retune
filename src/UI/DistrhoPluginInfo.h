// DistrhoPluginInfo.h — what DPF needs to know to build Retune's editor, as NoteGran's.
//
// The DISTRHO_PLUGIN_* identity macros are inert: Retune's CLAP entry point is its own
// (src/plugin.cpp), since DPF's CLAP wrapper can't take MIDI 2.0 notes or be a note effect. DPF is
// only the editor (UIExporter + NanoVG + pugl), and the DISTRHO_UI_* macros do bind.

#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND     "Unlikelyware"
#define DISTRHO_PLUGIN_NAME      "Retune"
#define DISTRHO_PLUGIN_URI       "https://unlikelyware.com/retune"
#define DISTRHO_PLUGIN_CLAP_ID   "com.unlikelyware.retune"
#define DISTRHO_PLUGIN_BRAND_ID  Unlk
#define DISTRHO_PLUGIN_UNIQUE_ID Rtun

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
#define DISTRHO_PLUGIN_NUM_INPUTS      0
#define DISTRHO_PLUGIN_NUM_OUTPUTS     0
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 1
#define DISTRHO_PLUGIN_WANT_PROGRAMS   0
#define DISTRHO_PLUGIN_WANT_STATE      0
// Gates UI::getPluginInstancePointer(), which gives the editor the clap_plugin_t UIExporter was
// built with.
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1

#define DISTRHO_UI_USE_NANOVG     1
#define DISTRHO_UI_USER_RESIZABLE 1
#define DISTRHO_UI_DEFAULT_WIDTH  520
#define DISTRHO_UI_DEFAULT_HEIGHT 560

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
