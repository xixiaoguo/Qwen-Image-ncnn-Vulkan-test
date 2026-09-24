#include "ui_main.h"
#include "i18n.h"

#include <FL/Fl.H>
#include <FL/Fl_Shared_Image.H>

#include <algorithm>
#include <cstdlib>
#include <cstring>

// Pick a sensible startup language: honour LANG/LC_ALL when it starts with
// "zh" (e.g. zh_CN.UTF-8, zh_TW.UTF-8), otherwise fall back to English.
static Lang detect_startup_language() {
    const char *envs[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
    for (const char *e : envs) {
        const char *v = std::getenv(e);
        if (v && std::strlen(v) >= 2 &&
            (v[0] == 'z' || v[0] == 'Z') && (v[1] == 'h' || v[1] == 'H')) {
            return Lang::ChineseSimplified;
        }
    }
    return Lang::English;
}

// Open the window at a size that suits the actual screen: generous on a large
// desktop panel, clipped to the display on a small one.
static void compute_window_size(int &w, int &h) {
    const int sw = Fl::w();
    const int sh = Fl::h();

    w = std::min(2040, (int)(sw * 0.82));
    h = std::min(1300, (int)(sh * 0.90));
    if (w < 1400) w = std::min(1400, sw);
    if (h < 1000) h = std::min(1000, sh);
}

int main(int argc, char **argv) {
    // Enable FLTK's threading support *before* the first worker thread exists.
    // Without this, Fl::awake() calls coming from the generation worker are
    // silently dropped: the output never reaches the log and the window stays
    // stuck on "running..." even after the generator has exited.
    Fl::lock();

    Fl::scheme("gtk+");
    configure_ui_font();

    // Enable the image formats FLTK can load (PNG/JPEG/...), used by the
    // picker's thumbnail pane.
    fl_register_images();

    Lang start = detect_startup_language();
    i18n_set_language(start);

    int w = 0, h = 0;
    compute_window_size(w, h);

    MainWindow *win = new MainWindow(w, h, tr(Str::AppTitle));
    win->apply_language(start);
    win->show(argc, argv);
    return Fl::run();
}
