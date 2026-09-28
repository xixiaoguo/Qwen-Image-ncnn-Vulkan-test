// wheel_guard.h - keep mouse-wheel events where the pointer actually is
#pragma once

#include <FL/Fl.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_File_Browser.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Text_Display.H>

// FLTK hands mouse-wheel events to widgets the pointer is not over. A probe
// showed an Fl_Text_Display scrolling away while the pointer sat in a blank
// area on the other side of the window, and an Fl_Scroll does the same. So
// every widget that reacts to the wheel has to check the pointer itself; this
// wrapper adds exactly that check and nothing else.
template <class Base>
class WheelGuard : public Base {
public:
    WheelGuard(int X, int Y, int W, int H, const char *L = nullptr)
        : Base(X, Y, W, H, L) {}

    int handle(int event) override {
        if (event == FL_MOUSEWHEEL && !Fl::event_inside(this)) return 0;
        return Base::handle(event);
    }
};

using GuardedScroll      = WheelGuard<Fl_Scroll>;
using GuardedChoice      = WheelGuard<Fl_Choice>;
using GuardedTextDisplay = WheelGuard<Fl_Text_Display>;
using GuardedFileBrowser = WheelGuard<Fl_File_Browser>;
