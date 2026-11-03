// src/UI/BackingScale_mac.mm — the live Retina scale of the window the editor is embedded in.
//
// DPF reads its scale factor once, when the UIExporter is built. At that moment the view is not yet in
// the host's window, so pugl falls back to [NSScreen mainScreen] — the screen with keyboard focus,
// not necessarily the one the editor opens on — and the value is never refreshed when the host
// window moves to a screen with a different backing scale. Pugl itself, meanwhile, converts mouse
// positions and view frames with the window's CURRENT screen. Sizing the editor from DPF's cached
// value therefore leaves the drawing and the mouse in different coordinate spaces: a UI drawn at 2x
// on a 1x screen (huge, clipped), or one that draws but whose controls miss their clicks.
// RetuneUI.cpp (after NoteGran's, after StruckSynth's) sizes from this instead, and re-applies the size whenever it changes.

#import <Cocoa/Cocoa.h>

#include <cstdint>

extern "C" double rt_gui_backing_scale(uintptr_t nsView)
{
    NSView* const view = (__bridge NSView*)(void*)nsView;
    if (NSWindow* const window = (view != nil) ? [view window] : nil)
        return [window backingScaleFactor];
    NSScreen* const screen = [NSScreen mainScreen];
    return screen != nil ? [screen backingScaleFactor] : 1.0;
}
