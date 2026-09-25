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

// Target shape of the window, as width / height. Lower it for a narrower
// window: 1.78 = 16:9, 1.60 = 16:10, 1.50 = 3:2, 1.33 = 4:3.
static const double kWindowAspect = 1.50;

// Narrowest window the layout can hold. The parameter column plus the preview
// toolbar on the right (the last button on that row ends around 990 px at the
// default font) need about this much; below it those controls run off the edge.
static const int kWindowMinWidth = 1050;

// Open the window at a size that suits the actual screen: tall enough to show
// the whole parameter panel, and as wide as the target aspect ratio allows
// rather than as wide as the display happens to be.
static void compute_window_size(int &w, int &h) {
    const int sw = Fl::w();
    const int sh = Fl::h();

    h = std::min(1300, (int)(sh * 0.90));
    if (h < 1000) h = std::min(1000, sh);

    w = std::min(std::min(2040, sw), (int)(h * kWindowAspect));
    if (w < kWindowMinWidth) w = std::min(kWindowMinWidth, sw);
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

    // The environment only supplies the initial default: the constructor
    // loads the config file, and a stored "language" there wins over it.
    Lang start = detect_startup_language();
    i18n_set_language(start);

    int w = 0, h = 0;
    compute_window_size(w, h);

    // The widgets are built before the config file is applied, so they still
    // carry the environment language at this point. Retranslate them to
    // whatever language is active *now* - passing `start` here would override
    // the stored language and ignore the user's choice on every launch.
    MainWindow *win = new MainWindow(w, h, tr(Str::AppTitle));
    win->apply_language(i18n_language());
    win->show(argc, argv);
    return Fl::run();
}
