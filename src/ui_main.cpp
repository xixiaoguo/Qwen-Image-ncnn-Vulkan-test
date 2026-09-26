#include "ui_main.h"
#include "image_info.h"
#include "picker.h"
#include "settings.h"
#include "wheel_guard.h"

#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/filename.H>   // fl_open_uri() for the About button

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

#include <dirent.h>
#include <limits.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// ------------------------------------------------------------- appearance ---

namespace {

// The project page behind the About button.
const char *kRepoUrl = "https://github.com/xixiaoguo/Qwen-Image-ncnn-Vulkan-test";


const Fl_Color kFieldBg   = fl_rgb_color(0xff, 0xff, 0xff);  // text fields
const Fl_Color kButtonBg  = fl_rgb_color(0xff, 0xff, 0xff);  // flat buttons
const Fl_Color kSepColor  = fl_rgb_color(0xc9, 0xcf, 0xd8);  // separators
const Fl_Color kHeading   = fl_rgb_color(0x28, 0x31, 0x40);  // group titles
const Fl_Color kAccent    = fl_rgb_color(0x2e, 0x6f, 0xd6);  // selection / progress
const Fl_Color kGenerate  = fl_rgb_color(0x2e, 0x7d, 0x32);  // primary action
const Fl_Color kStop      = fl_rgb_color(0xc0, 0x2f, 0x2f);  // destructive action
const Fl_Color kLogBg     = fl_rgb_color(0x1e, 0x21, 0x26);  // console
const Fl_Color kLogFg     = fl_rgb_color(0xd6, 0xda, 0xe0);

// ---- UI metrics -----------------------------------------------------------
// Everything derives from one base font size, so the whole interface can be
// scaled from a single setting (ui_font_size in the config file). The default
// targets a plain 96 dpi desktop; under desktop scaling the toolkit already
// hands us a smaller logical screen, so a large font here would look huge.
struct Metrics {
    int font_base  = 15;   // labels and input text
    int font_title = 17;   // group headers, primary buttons
    int font_log   = 14;   // console text
    int row_h      = 32;   // standard control height
    int pad        = 16;   // window margin
    int gap        = 12;   // gutter between rows
    int left_w     = 540;  // width of the parameter column
    int label_w    = 150;  // space reserved for inline labels
    int action_h   = 36;   // bottom action row height
};

Metrics g_m;

// Pick a font size that keeps the interface visually consistent across
// toolkits and desktops. Both backends already folded their scaling into
// Fl::h(): X11 reports a logical 1991x1057 with scale 1.5 on this machine,
// Wayland reports 1280x720 with scale 1.0 (the compositor scales the buffer).
// Deriving from the logical height therefore lands on a sensible size either
// way, with no configuration needed.
int auto_font_size() {
    const int h = Fl::h();
    // Calibrated against this display: 2560x1440 at ~189 dpi, where a regular
    // desktop UI font works out to roughly 27 physical pixels. X11 reports a
    // logical 1057 with scale 1.5, Wayland a logical 720 with scale 1.0, so the
    // divisor below lands both near that target (~24 resp. ~22 physical px).
    int base = h / 55;             // 720 -> 13, 1080 -> 19, 1440 -> 26
    if (base < 9)  base = 9;
    if (base > 22) base = 22;
    return base;
}

void metrics_from_font(int base) {
    if (base < 10) base = 10;
    if (base > 32) base = 32;
    Metrics m;
    m.font_base  = base;
    m.font_title = base + 2;
    m.font_log   = base > 12 ? base - 1 : base;
    // Controls sit at ~1.9x the font size: tall enough to look calm, tight
    // enough that the parameter column does not waste vertical space.
    m.row_h      = (base * 19) / 10;
    m.pad        = base + 1;
    m.gap        = (base * 3) / 5;
    m.left_w     = base * 36;
    m.label_w    = base * 10;
    m.action_h   = (base * 22) / 10;
    g_m = m;
}

// Create a directory and any missing parents. Returns true when the path
// exists (or was created) as a directory afterwards.
bool ensure_directory(const std::string &path) {
    if (path.empty()) return false;
    struct stat st;
    if (stat(path.c_str(), &st) == 0) return S_ISDIR(st.st_mode) != 0;

    std::string built;
    if (path[0] == '/') built = "/";
    size_t i = 0;
    while (i < path.size()) {
        size_t slash = path.find('/', i);
        std::string part = path.substr(i, slash == std::string::npos ? std::string::npos : slash - i);
        if (!part.empty()) {
            if (!built.empty() && built.back() != '/') built += '/';
            built += part;
            mkdir(built.c_str(), 0755);
        }
        if (slash == std::string::npos) break;
        i = slash + 1;
    }
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode) != 0;
}

// Is this field fit to be written down? A number, and not below `min_value`.
// Anything else - empty, half-typed, plainly wrong - is left out of the config
// file and out of saved presets, so a stray keystroke cannot come back as a
// broken setting on the next start.
bool number_ok(const char *s, double min_value) {
    if (!s || !*s) return false;
    char *end = nullptr;
    const double v = strtod(s, &end);
    if (end == s) return false;
    while (*end == ' ' || *end == '\t') ++end;
    if (*end) return false;
    return v >= min_value;
}

bool int_ok(const char *s, long long min_value) {
    if (!s || !*s) return false;
    char *end = nullptr;
    const long long v = strtoll(s, &end, 10);
    if (end == s) return false;
    while (*end == ' ' || *end == '\t') ++end;
    if (*end) return false;
    return v >= min_value;
}

// "left,top,right,bottom" - four non-negative numbers, the shape both the
// generator and the config file expect for -x.
bool outpaint_ok(const char *s) {
    if (!s || !*s) return false;
    int l = 0, t = 0, r = 0, b = 0;
    char extra = 0;
    if (sscanf(s, "%d,%d,%d,%d%c", &l, &t, &r, &b, &extra) != 4) return false;
    return l >= 0 && t >= 0 && r >= 0 && b >= 0;
}

bool file_exists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) != 0;
}

// Directory holding the running executable, resolved through /proc/self/exe so
// it works no matter how the binary was launched (symlink, PATH, relative).
std::string resolve_app_dir() {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    buf[n] = '\0';
    std::string p(buf);
    size_t slash = p.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return ".";
    return p.substr(0, slash);
}

// A file:// URI for a local path, percent-encoded so spaces and non-ASCII
// bytes survive the trip through the shell into dbus-send.
std::string path_to_file_uri(const std::string &path) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string uri = "file://";
    for (unsigned char c : path) {
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '/' || c == '-' ||
                           c == '_' || c == '.' || c == '~';
        if (plain) {
            uri += (char)c;
        } else {
            uri += '%';
            uri += kHex[c >> 4];
            uri += kHex[c & 0x0f];
        }
    }
    return uri;
}

// Fields carried by each kind of preset. The prompt rows hold a single "text"
// value; the parameter row holds the options that describe how to generate.
const std::vector<std::string> kPromptFields = {"text"};
const std::vector<std::string> kParamFields = {
    "cfg", "ctrl_scale", "width", "height", "steps", "z_steps", "seed", "batch",
    "random_seed", "gpu", "inputs", "zinput", "mask", "control", "outpaint"};

// The values the form starts with. The built-in "Default" entry of every preset
// row restores exactly these. They are compiled in on purpose: they describe how
// the program behaves out of the box, not a user preference, so they never take
// up room in the config file.
constexpr const char *kDefCfg    = "1.0";
constexpr const char *kDefWidth  = "1024";
constexpr const char *kDefHeight = "1024";
constexpr const char *kDefSteps  = "40";
constexpr const char *kDefSeed   = "42";
constexpr const char *kDefBatch  = "1";
constexpr const char *kDefCscale = "1.0";   // zimage ControlNet scale
constexpr const char *kDefOutpaint = "128,128,128,128";  // the manual's own example
constexpr int         kDefGpu    = 0;      // index of "auto" in the GPU dropdown

// Program version. It goes into the config file as an identifier and follows
// the window title, so a bug report can say which build it came from.
constexpr const char *kAppVersion = "1.1";

// Cell metrics of the thumbnail grid. GalleryView computes the same values from
// the same rule; the minimum window width depends on them, and the two have to
// agree or the grid would quietly drop below four columns.
int grid_cell_w() { return std::max(90, ui_font_base() * 11); }
int grid_gap()    { return std::max(6, ui_font_base() / 2); }

// The console colours its lines through a style buffer running alongside the
// text: 'A' is ordinary output, 'B' marks a warning or an error.
Fl_Text_Display::Style_Table_Entry g_log_styles[] = {
    {FL_BLACK, FL_HELVETICA, 14, 0},   // 'A'
    {FL_RED,   FL_HELVETICA, 14, 0},   // 'B'
};

// Does this line deserve the red pen? Our own failures carry a bracket tag;
// the generator writes its complaints to stderr, and since both streams land in
// the same console we recognise those by their wording.
bool is_problem_line(const std::string &s) {
    static const char *const kMarks[] = {
        "[error]", "[ERROR]", "[参数无效]", "[失败]", "[模型]",
        "must be", "must match", "insufficient", "not found",
        "failed", "invalid",
    };
    for (const char *m : kMarks)
        if (s.find(m) != std::string::npos) return true;
    return false;
}

// The five ways zimage-ncnn-vulkan is driven. They share one binary but pass
// different options: plain text-to-image, LanPaint inpaint (-k), LanPaint
// outpaint (-x), ControlNet (-c -w) and the tile upscaler (-c -t). The mode
// picker decides which of the image rows the form shows.
enum class ZMode {
    TextToImage = 0,
    Inpaint = 1,
    Outpaint = 2,
    ControlNet = 3,
    TileUpscale = 4,
};

// Z-Image ships two models. ControlNet (and the tile upscaler that uses its
// weights) only exists for the turbo one, so the model choice gates those two
// modes.
enum class ZModel {
    Turbo = 0,   // z-image-turbo
    Base = 1,    // z-image
};

const char *kZModelTurbo = "z-image-turbo";
const char *kZModelBase  = "z-image";

const char *zmodel_dir(ZModel m) {
    return m == ZModel::Base ? kZModelBase : kZModelTurbo;
}

// Stable name for the config file / presets.
const char *zmode_key(ZMode m) {
    switch (m) {
        case ZMode::Inpaint:     return "inpaint";
        case ZMode::Outpaint:    return "outpaint";
        case ZMode::ControlNet:  return "control";
        case ZMode::TileUpscale: return "tile";
        case ZMode::TextToImage: break;
    }
    return "text";
}

ZMode zmode_from_key(const std::string &s) {
    if (s == "inpaint")  return ZMode::Inpaint;
    if (s == "outpaint") return ZMode::Outpaint;
    if (s == "control")  return ZMode::ControlNet;
    if (s == "tile")     return ZMode::TileUpscale;
    return ZMode::TextToImage;
}

ZModel zmodel_from_key(const std::string &s) {
    return s == "base" ? ZModel::Base : ZModel::Turbo;
}

} // namespace

// Exposed to the rest of the program (the picker follows the same scale).
int ui_font_base() { return g_m.font_base; }

// Fl_Text_Buffer::text() returns freshly allocated memory the caller owns.
std::string buffer_text(const Fl_Text_Buffer *b) {
    if (!b) return "";
    char *t = b->text();
    std::string out = t ? t : "";
    free(t);
    return out;
}

// ---------------------------------------------------------------- layout ---

MainWindow::MainWindow(int W, int H, const char *L)
    : Fl_Double_Window(W, H, L) {

    // Global palette, applied before the first child widget is created.
    // The window floor uses the same colour the KDE title bar is drawn with
    // (#DEE0E2 on this desktop), so the title bar and the client area read as
    // one surface instead of two stacked greys.
    // Global palette, applied before the first child widget is created.
    Fl::background(0xf1, 0xf2, 0xf5);
    Fl::background2(0xff, 0xff, 0xff);
    Fl::foreground(0x1f, 0x24, 0x2b);

    // The generator and the model always live next to this executable.
    m_app_dir = resolve_app_dir();
    m_avail   = detect_backends(m_app_dir);

    // The config kept the program's old name for a while: take the new one and
    // move an existing file across, so nobody loses their presets on upgrade.
    const std::string cfg_path = m_app_dir + "/Image-ncnn-Vulkan-UI.conf";
    const std::string old_cfg  = m_app_dir + "/qwenimage-gui.conf";
    if (access(cfg_path.c_str(), F_OK) != 0 && access(old_cfg.c_str(), F_OK) == 0)
        rename(old_cfg.c_str(), cfg_path.c_str());
    m_settings = std::make_unique<Settings>(cfg_path);
    m_settings->load();

    // Which engine to open on. With only one binary installed there is nothing
    // to choose: the window pins itself to it and never shows the switcher.
    // With both (or neither) the stored choice wins, and the default is Qwen,
    // which is what this program grew out of.
    {
        Backend start = Backend::Qwen;
        const std::string stored = m_settings->get("backend", "");
        if (stored == "zimage")      start = Backend::ZImage;
        else if (stored == "qwen")   start = Backend::Qwen;
        else if (m_avail.zimage && !m_avail.qwen) start = Backend::ZImage;
        m_backend = start;
    }
    m_exe_path   = m_app_dir + "/" + backend_spec(m_backend).exe_name;
    m_model_path = resolve_model_path(m_app_dir, m_backend);

    // Each engine starts from its own documented defaults; the config file
    // (read below) may override either set.
    m_params[(int)Backend::Qwen]   = default_params(Backend::Qwen);
    m_params[(int)Backend::ZImage] = default_params(Backend::ZImage);

    // Derive every UI metric from the font size *before* the first widget is
    // created: the sizes feed both styling and layout. The size is detected
    // from the display; ui_font_size in the config file only overrides it.
    const int cfg_font = m_settings->get_int("ui_font_size", 0);
    metrics_from_font(cfg_font > 0 ? cfg_font : auto_font_size());

    build_widgets();
    style_widgets();
    layout_widgets();
    end();

    // Minimum size follows the metrics instead of hard-coded pixels, and the
    // width has to be enough for four grid columns - the cell size is fixed, so
    // anything narrower would silently fall back to three.
    size_range(grid_min_width(), g_m.row_h * 20);
    if (w() < grid_min_width()) size(grid_min_width(), h());

    load_settings();

    // Preview the output folder right away: the grid is the window's opening
    // view, not something the user has to ask for.
    m_gallery->set_directory(output_dir());
    refresh_gallery();

    // Layout self-check: run with QWEN_DUMP_LAYOUT=1 to print every widget's
    // geometry, which makes overlap/overflow problems easy to spot.
    if (getenv("QWEN_DUMP_LAYOUT")) {
        fprintf(stderr, "--- layout dump: window %dx%d, base font %d ---\n",
                w(), h(), g_m.font_base);
        std::function<void(Fl_Group *, int, int, int)> walk =
            [&](Fl_Group *g, int ox, int oy, int depth) {
            for (int i = 0; i < g->children(); ++i) {
                Fl_Widget *c = g->child(i);
                if (!c) continue;
                int ax = ox + c->x(), ay = oy + c->y();
                fprintf(stderr, "%*s[%2d] %-24s x=%4d y=%4d w=%4d h=%4d | abs=(%4d,%4d)\n",
                        depth * 2, "", i, c->label() ? c->label() : "(no label)",
                        c->x(), c->y(), c->w(), c->h(), ax, ay);
                if (auto *sub = dynamic_cast<Fl_Group *>(c)) walk(sub, ax, ay, depth + 1);
            }
        };
        walk(this, 0, 0, 0);

        // The numbers the controls actually hold after the config was applied.
        fprintf(stderr, "[values] width=%s height=%s steps=%s seed=%s batch=%s cfg=%s gpu=%s\n",
                m_width->value(), m_height->value(), m_steps->value(),
                m_seed->value(), m_batch->value(), m_cfg->value(), m_gpu->text());
    }

    // The console stays quiet at startup: it is meant for the generator's own
    // output. Only a real problem is worth a line here.
}

MainWindow::~MainWindow() {
    Fl::remove_timeout(cb_run_tick, this);   // the clock must not outlive the window
    if (m_running) {
        if (m_child_pid > 0) kill(m_child_pid, SIGTERM);
        if (m_worker.joinable()) m_worker.join();
    }
    save_settings();   // safe: the text buffers are still alive here

    // The text widgets point into these buffers, and Fl_Group's destructor -
    // which deletes the children - runs after this body. Destroy the widgets
    // first, or every editor tears its callbacks off a buffer that is already
    // gone.
    clear();
    delete m_prompt_buf;
    delete m_negative_buf;
    delete m_log_buf;
    delete m_log_style;
    m_log_buf = nullptr;
    m_log_style = nullptr;
}

void MainWindow::resize(int X, int Y, int W, int H) {
    Fl_Double_Window::resize(X, Y, W, H);
    if (m_gallery) layout_widgets();   // ignored until the widgets exist
}

void MainWindow::show() {
    Fl_Double_Window::show();

    // FLTK focuses the first focusable child, which is the language dropdown.
    // Hand the caret to the prompt box instead: that is what the user types
    // into, and it also keeps the dropdown from opening on a stray keypress.
    Fl::focus(m_prompt);
    if (getenv("QWEN_DUMP_LAYOUT"))
        fprintf(stderr, "[focus] prompt=%p  Fl::focus()=%p  %s\n",
                (void *)m_prompt, (void *)Fl::focus(),
                Fl::focus() == m_prompt ? "OK" : "MISMATCH");
}

void MainWindow::hide() {
    save_settings();
    Fl_Double_Window::hide();
}

int MainWindow::handle(int event) {
    // Esc leaves the detail view, exactly like the "Grid" button does. The
    // window only sees the key when no control wanted it first, which is the
    // behaviour we are after.
    if (event == FL_KEYDOWN && Fl::event_key() == FL_Escape &&
        m_gallery && m_gallery->mode() == GalleryView::Mode::Detail) {
        m_gallery->set_mode(GalleryView::Mode::Grid);
        return 1;
    }
    return Fl_Double_Window::handle(event);
}

// --------------------------------------------------------------- widgets ---

int MainWindow::grid_min_width() const {
    // The parameter column plus whatever the preview needs for four grid cells
    // side by side, their gutters and the scrollbar. Keeping the cell size fixed
    // is what makes the grid readable; the window width is what gives way.
    const int cell_w = grid_cell_w();
    const int gap = grid_gap();
    const int scrollbar = std::max(1, Fl::scrollbar_size());
    const int preview = 4 * cell_w + 5 * gap + scrollbar + g_m.pad;
    return g_m.pad + g_m.left_w + g_m.pad + preview;
}

void MainWindow::build_widgets() {
    // --- engine selector (top row) ---
    // Built only when both generators are installed: with one binary there is
    // nothing to switch, and the window simply is that engine's form. The
    // label is created alongside so neither can be shown without the other.
    if (m_avail.both()) {
        m_lbl_backend = new Fl_Box(FL_NO_BOX, 0, 0, 0, 0, tr(Str::Backend));
        m_lbl_backend->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

        m_backend_choice = new GuardedChoice(0, 0, 0, 0);
        m_backend_choice->add(backend_spec(Backend::Qwen).label);    // index 0
        m_backend_choice->add(backend_spec(Backend::ZImage).label);  // index 1
        m_backend_choice->value(m_backend == Backend::ZImage ? 1 : 0);
        m_backend_choice->callback(cb_backend, this);
    }

    // --- language selector (top row) ---
    m_lbl_lang = new Fl_Box(FL_NO_BOX, 0, 0, 0, 0, tr(Str::Language));
    m_lbl_lang->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    m_lang_choice = new GuardedChoice(0, 0, 0, 0);
    m_lang_choice->add(language_name(Lang::English));            // index 0
    m_lang_choice->add(language_name(Lang::ChineseSimplified));  // index 1
    m_lang_choice->value(i18n_language() == Lang::ChineseSimplified ? 1 : 0);
    m_lang_choice->callback(cb_language, this);

    // --- about (top row, far corner) ---
    m_btn_about = new Fl_Button(0, 0, 0, 0, tr(Str::About));
    m_btn_about->callback(cb_about, this);
    m_btn_about->tooltip(kRepoUrl);

    // --- decoration ---
    m_sep_top = new Fl_Box(FL_THIN_DOWN_BOX, 0, 0, 0, 0, "");
    m_sep_top->color(kSepColor);

    m_grp_gen = new Fl_Box(FL_NO_BOX, 0, 0, 0, 0, tr(Str::GroupGeneration));
    m_grp_gen->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    m_sep_ref = new Fl_Box(FL_THIN_DOWN_BOX, 0, 0, 0, 0, "");
    m_sep_ref->color(kSepColor);

    m_size_x = new Fl_Box(FL_NO_BOX, 0, 0, 0, 0, "x");
    m_size_x->align(FL_ALIGN_CENTER);

    // --- prompt ---
    m_lbl_prompt = new Fl_Box(FL_NO_BOX, 0, 0, 0, 0, tr(Str::PromptRequired));
    m_lbl_prompt->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_prompt_buf = new Fl_Text_Buffer();
    m_prompt = new Hint_Text_Editor(0, 0, 0, 0);
    m_prompt->buffer(m_prompt_buf);
    m_prompt->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
    m_prompt->linenumber_width(0);

    // --- negative prompt ---
    m_lbl_negative = new Fl_Box(FL_NO_BOX, 0, 0, 0, 0, tr(Str::NegativeOptional));
    m_lbl_negative->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_negative_buf = new Fl_Text_Buffer();
    m_negative = new Hint_Text_Editor(0, 0, 0, 0);
    m_negative->buffer(m_negative_buf);
    m_negative->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
    m_negative->linenumber_width(0);

    // --- CFG ---
    m_cfg = new Hint_Float_Input(0, 0, 0, 0, tr(Str::Cfg));
    m_lbl_cfg = m_cfg;
    m_cfg->value(kDefCfg);

    // --- output: folder + file name + format ---
    m_output_dir = new Hint_Input(0, 0, 0, 0, tr(Str::OutputDir));
    m_lbl_output_dir = m_output_dir;
    m_output_dir->value(".");

    m_btn_browse_output = new Fl_Button(0, 0, 0, 0, tr(Str::Browse));
    m_btn_browse_output->callback(cb_browse_output, this);

    m_output_name = new Hint_Input(0, 0, 0, 0, tr(Str::OutputName));
    m_lbl_output_name = m_output_name;
    m_output_name->value("out");

    m_output_format = new GuardedChoice(0, 0, 0, 0, tr(Str::OutputFormat));
    m_lbl_output_format = m_output_format;
    // Exactly the formats the generator documents, and no more: a png/jpg/webp
    // suffix is what it switches on. Any other extension (bmp, tif, ...) is
    // quietly written as PNG data under that misleading name.
    m_output_format->add("png");
    m_output_format->add("jpg");
    m_output_format->add("webp");
    m_output_format->value(0);

    // --- size ---
    m_width = new Hint_Int_Input(0, 0, 0, 0, tr(Str::Size));
    m_lbl_size = m_width;
    m_width->value(kDefWidth);

    m_height = new Hint_Int_Input(0, 0, 0, 0);
    m_height->value(kDefHeight);

    // Sizes are all multiples of 32 so they satisfy both text-to-image (16) and
    // image-editing (32) validation. "720p"/"1080p" entries sit on the nearest
    // conforming size: 720p -> 736 lines, 1080p -> 1088 lines.
    m_preset_menu = new Fl_Menu_Button(0, 0, 0, 0, tr(Str::Preset));
    m_preset_menu->add("512 x 512");
    m_preset_menu->add("768 x 768");
    m_preset_menu->add("1024 x 1024");
    m_preset_menu->add("1024 x 768");
    m_preset_menu->add("1280 x 736   (720p)");
    m_preset_menu->add("1920 x 1088  (1080p)");
    m_preset_menu->add("2560 x 1440  (1440p)");
    m_preset_menu->add("2048 x 2048");
    auto cb_size_preset = [](Fl_Widget *w, void *d) {
        auto *self = static_cast<MainWindow *>(d);
        auto *mb = static_cast<Fl_Menu_Button *>(w);
        const Fl_Menu_Item *item = mb->mvalue();
        int ww = 0, hh = 0;
        if (item && item->label() && sscanf(item->label(), "%d x %d", &ww, &hh) == 2) {
            self->m_width->value(std::to_string(ww).c_str());
            self->m_height->value(std::to_string(hh).c_str());
        }
        Fl::focus(nullptr);   // drop the dotted focus ring
    };
    m_preset_menu->callback(cb_size_preset, this);

    // --- steps / seed ---
    m_steps = new Hint_Int_Input(0, 0, 0, 0, tr(Str::Steps));
    m_lbl_steps = m_steps;
    m_steps->value(kDefSteps);

    m_seed = new Hint_Int_Input(0, 0, 0, 0, tr(Str::Seed));
    m_lbl_seed = m_seed;
    m_seed->value(kDefSeed);

    // --- batch / random seed ---
    m_batch = new Hint_Int_Input(0, 0, 0, 0, tr(Str::Batch));
    m_lbl_batch = m_batch;
    m_batch->value(kDefBatch);

    m_random_seed = new Fl_Check_Button(0, 0, 0, 0, tr(Str::RandomSeed));

    // --- GPU ---
    m_gpu = new GuardedChoice(0, 0, 0, 0, tr(Str::Gpu));
    m_lbl_gpu = m_gpu;
    m_gpu->add("auto");   // index 0 -> INT_MAX (omit -g)
    m_gpu->add("cpu");    // index 1 -> -1
    m_gpu->add("0");
    m_gpu->add("1");
    m_gpu->add("2");
    m_gpu->add("3");
    m_gpu->value(kDefGpu);

    // --- reference images ---
    m_lbl_ref = new Fl_Box(FL_NO_BOX, 0, 0, 0, 0, tr(Str::RefImages));
    m_lbl_ref->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    m_inputs = new Hint_Browser(0, 0, 0, 0);

    m_btn_add_image = new Fl_Button(0, 0, 0, 0, tr(Str::AddImage));
    m_btn_add_image->callback(cb_add_input, this);
    m_btn_del_image = new Fl_Button(0, 0, 0, 0, tr(Str::RemoveSelected));
    m_btn_del_image->callback(cb_del_input, this);

    // --- Z-Image only: the mode picker and the inputs each mode needs ---
    // Created unconditionally (the layout only places them for that backend),
    // so switching engines never has to build or destroy widgets.
    m_lbl_zmode = new Fl_Box(FL_NO_BOX, 0, 0, 0, 0, tr(Str::ZMode));
    m_lbl_zmode->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    m_lbl_zmodel = new Fl_Box(FL_NO_BOX, 0, 0, 0, 0, tr(Str::ZModel));
    m_lbl_zmodel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    m_zmodel = new GuardedChoice(0, 0, 0, 0);
    m_zmodel->add(kZModelTurbo);   // 0
    m_zmodel->add(kZModelBase);    // 1
    m_zmodel->value((int)ZModel::Turbo);
    m_zmodel->callback(cb_zmodel, this);

    m_zmode = new GuardedChoice(0, 0, 0, 0);
    m_zmode->add(tr(Str::ZModeText));      // 0
    m_zmode->add(tr(Str::ZModeInpaint));   // 1
    m_zmode->add(tr(Str::ZModeOutpaint));  // 2
    m_zmode->add(tr(Str::ZModeControl));   // 3
    m_zmode->add(tr(Str::ZModeTile));      // 4
    m_zmode->value((int)ZMode::TextToImage);
    m_zmode->callback(cb_zmode, this);

    m_zinput = new Hint_Input(0, 0, 0, 0, tr(Str::ZImageInput));
    // Outpaint derives its output size from this picture, so every edit to the
    // path is a reason to recompute.
    m_zinput->when(FL_WHEN_CHANGED);
    m_zinput->callback(cb_zinput_changed, this);
    m_btn_browse_zinput = new Fl_Button(0, 0, 0, 0, tr(Str::Browse));
    m_btn_browse_zinput->callback(cb_browse_zinput, this);

    m_mask = new Hint_Input(0, 0, 0, 0, tr(Str::MaskImage));
    m_lbl_mask = m_mask;
    m_mask->tooltip(tr(Str::MaskHint));
    m_btn_browse_mask = new Fl_Button(0, 0, 0, 0, tr(Str::Browse));
    m_btn_browse_mask->callback(cb_browse_mask, this);

    m_control = new Hint_Input(0, 0, 0, 0, tr(Str::ControlImage));
    m_lbl_control = m_control;
    m_btn_browse_control = new Fl_Button(0, 0, 0, 0, tr(Str::Browse));
    m_btn_browse_control->callback(cb_browse_control, this);

    m_control_scale = new Hint_Float_Input(0, 0, 0, 0, tr(Str::ControlScale));
    m_lbl_control_scale = m_control_scale;
    m_control_scale->value("1.0");

    // -x ships with the margins the manual uses in its own example: an empty
    // box gave no clue at all about the expected "left,top,right,bottom" form.
    m_outpaint = new Hint_Input(0, 0, 0, 0, tr(Str::Outpaint));
    m_lbl_outpaint = m_outpaint;
    m_outpaint->value(kDefOutpaint);
    m_outpaint->tooltip(tr(Str::OutpaintHint));
    m_outpaint->when(FL_WHEN_CHANGED);
    m_outpaint->callback(cb_outpaint_changed, this);

    // --- preview toolbar ---
    m_btn_gallery = new Fl_Button(0, 0, 0, 0, tr(Str::GalleryBack));
    m_btn_gallery->callback(cb_gallery_back, this);
    m_btn_latest = new Fl_Button(0, 0, 0, 0, tr(Str::PreviewLatest));
    m_btn_latest->callback(cb_latest, this);
    m_btn_refresh = new Fl_Button(0, 0, 0, 0, tr(Str::Reload));
    m_btn_refresh->callback(cb_refresh, this);

    m_btn_fit = new Fl_Button(0, 0, 0, 0, tr(Str::Fit));
    m_btn_fit->callback(cb_zoom_fit, this);
    m_btn_1to1 = new Fl_Button(0, 0, 0, 0, tr(Str::Zoom1to1));
    m_btn_1to1->callback(cb_zoom_1to1, this);

    m_btn_open_dir = new Fl_Button(0, 0, 0, 0, tr(Str::OpenOutputDir));
    m_btn_open_dir->callback(cb_open_output_dir, this);

    // --- preview + console ---
    m_gallery = new GalleryView(0, 0, 0, 0);
    m_gallery->on_mode_change = [this] { update_view_buttons(); };
    m_gallery->on_error = [this](const std::string &e) {
        log("[preview] cannot load: " + e);
    };

    m_log_view = new GuardedTextDisplay(0, 0, 0, 0);
    m_log_buf = new Fl_Text_Buffer();
    m_log_view->buffer(m_log_buf);
    // Wrap long lines at the widget edge. The printed command line alone is far
    // wider than this pane, and without wrapping the tail of it - and of any
    // long generator message - sits off-screen behind a horizontal scrollbar.
    m_log_view->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);

    // --- progress + actions ---
    m_progress = new Fl_Progress(0, 0, 0, 0);
    m_progress->minimum(0);
    m_progress->maximum(1);
    m_progress->value(0);

    m_btn_generate = new Fl_Button(0, 0, 0, 0, tr(Str::Generate));
    m_btn_generate->callback(cb_generate, this);
    m_btn_generate->shortcut(FL_F + 5);   // F5 starts a run

    m_btn_stop = new Fl_Button(0, 0, 0, 0, tr(Str::Stop));
    m_btn_stop->callback(cb_stop, this);
    m_btn_stop->deactivate();

    // --- named presets: one row per part of the form ---
    build_preset_row(m_prompt_presets, "prompt_preset");
    build_preset_row(m_negative_presets, "negative_preset");
    build_preset_row(m_param_presets, "param_preset");
}

void MainWindow::style_widgets() {
    // --- text fields ---
    for (Fl_Text_Editor *e : {m_prompt, m_negative}) {
        e->labelsize(g_m.font_base);
        e->textsize(g_m.font_base);
        e->textfont(FL_HELVETICA);
        e->color(kFieldBg);
        e->selection_color(kAccent);
    }

    Fl_Input_ *fields[] = {m_cfg, m_output_dir, m_output_name,
                           m_width, m_height, m_steps, m_seed, m_batch,
                           m_zinput, m_mask, m_control, m_control_scale, m_outpaint};
    for (Fl_Input_ *w : fields) {
        w->labelsize(g_m.font_base);
        w->textsize(g_m.font_base);
        w->color(kFieldBg);
        w->selection_color(kAccent);
    }

    // --- choices ---
    for (Fl_Choice *c : {m_gpu, m_lang_choice, m_backend_choice, m_zmodel, m_zmode, m_output_format}) {
        if (!c) continue;   // the engine switcher only exists with both binaries
        c->labelsize(g_m.font_base);
        c->textsize(g_m.font_base);
        c->color(kFieldBg);
        // No dotted focus ring around the dropdown once it has been used: it
        // reads as a stray artefact on a form like this one.
        c->clear_visible_focus();
    }

    // Dropdowns hand the focus back after a pick, so nothing keeps a ring on.
    // (The engine / language / mode boxes already do this inside their own
    // callbacks; the two plain pickers get it here.)
    auto cb_drop_focus = [](Fl_Widget *, void *) { Fl::focus(nullptr); };
    if (m_gpu)           m_gpu->callback(cb_drop_focus);
    if (m_output_format) m_output_format->callback(cb_drop_focus);

    m_preset_menu->labelsize(g_m.font_base);
    m_preset_menu->textsize(g_m.font_base);
    m_preset_menu->color(kButtonBg);
    m_preset_menu->clear_visible_focus();

    // --- plain labels ---
    for (Fl_Widget *w : {m_lbl_lang, m_lbl_backend, m_lbl_prompt, m_lbl_negative,
                         m_lbl_zmode, m_lbl_zmodel})
        if (w) w->labelsize(g_m.font_base);

    m_size_x->labelsize(g_m.font_base);

    // --- group headers ---
    m_grp_gen->labelfont(FL_HELVETICA_BOLD);
    m_grp_gen->labelsize(g_m.font_title);
    m_grp_gen->labelcolor(kHeading);

    m_lbl_ref->labelfont(FL_HELVETICA_BOLD);
    m_lbl_ref->labelsize(g_m.font_title);
    m_lbl_ref->labelcolor(kHeading);

    // --- buttons ---
    Fl_Button *flat[] = {m_btn_browse_output, m_btn_add_image, m_btn_del_image,
                         m_btn_browse_zinput, m_btn_browse_mask, m_btn_browse_control,
                         m_btn_gallery, m_btn_latest, m_btn_refresh,
                         m_btn_fit, m_btn_1to1,
                         m_btn_open_dir, m_btn_about};
    for (Fl_Button *b : flat) {
        b->labelsize(g_m.font_base);
        b->color(kButtonBg);   // stand out from the light-grey window floor
    }

    m_btn_generate->labelsize(g_m.font_title);
    m_btn_generate->labelfont(FL_HELVETICA_BOLD);
    m_btn_generate->color(kGenerate);
    m_btn_generate->labelcolor(FL_WHITE);

    m_btn_stop->labelsize(g_m.font_title);
    m_btn_stop->color(kStop);
    m_btn_stop->labelcolor(FL_WHITE);

    // --- console ---
    m_log_view->color(kLogBg);
    m_log_view->textcolor(kLogFg);
    m_log_view->textsize(g_m.font_log);
    m_log_view->selection_color(kAccent);

    // Red for problems, the console's own grey for everything else.
    g_log_styles[0].font = FL_HELVETICA;
    g_log_styles[1].font = FL_HELVETICA;
    g_log_styles[0].size = g_m.font_log;
    g_log_styles[1].size = g_m.font_log;
    g_log_styles[0].color = kLogFg;
    m_log_style = new Fl_Text_Buffer();
    m_log_view->highlight_data(m_log_style, g_log_styles,
                               (int)(sizeof(g_log_styles) / sizeof(g_log_styles[0])),
                               'A', nullptr, nullptr);

    // --- reference list ---
    m_inputs->labelsize(g_m.font_base);
    m_inputs->textsize(g_m.font_base);
    m_inputs->color(kFieldBg);

    // --- progress ---
    m_progress->labelsize(g_m.font_base);
    m_progress->color(kFieldBg);
    m_progress->selection_color(kAccent);

    m_random_seed->labelsize(g_m.font_base);

    // --- preset rows ---
    style_preset_row(m_prompt_presets);
    style_preset_row(m_negative_presets);
    style_preset_row(m_param_presets);
}

void MainWindow::layout_widgets() {
    // Two passes for the Z-Image form. Its image rows are short and fixed, so
    // the first pass (natural sizes) would leave a large empty area at the
    // bottom of the column; the second hands that leftover to the two prompt
    // boxes. The Qwen form has no such gap: its reference list already grows
    // into whatever is left.
    int bottom = 0;
    layout_pass(0, &bottom);
    if (m_backend == Backend::ZImage) {
        const int spare = (h() - g_m.pad) - bottom;
        if (spare > sc(24)) layout_pass(spare, &bottom);
    }
}

void MainWindow::layout_pass(int extra_prompt_h, int *bottom_out) {
    const int W = w(), H = h();

    // Every literal below is authored for the 20 px design font. S() scales it
    // to the size actually detected, so the layout cannot drift out of shape
    // when the interface is rendered smaller or larger.
    const auto S = [](int design) { return design * g_m.font_base / 20; };

    // Text areas grow with the window instead of being hard-coded.
    // These shrink with the window: the column has no scroll container, so the
    // text areas absorb whatever height is left after the fixed rows. The
    // coefficients are low because each prompt's label line carries a preset
    // row, and the parameter presets take a row of their own further down.
    // The Z-Image form adds four more rows (mask, control, scale/tile, outpaint),
    // so there the text areas give way a little earlier.
    const bool zimage = (m_backend == Backend::ZImage);
    const double prompt_frac   = zimage ? 0.075 : 0.140;
    const double negative_frac = zimage ? 0.055 : 0.100;
    int prompt_h   = std::max(S(40), std::min(S(220), (int)(H * prompt_frac)));
    int negative_h = std::max(S(32), std::min(S(160), (int)(H * negative_frac)));

    // The leftover from the first pass, split towards the prompt that matters
    // more. Both boxes get it, so the column ends where the window does.
    if (extra_prompt_h > 0) {
        const int to_prompt = extra_prompt_h * 55 / 100;
        prompt_h += to_prompt;
        negative_h += extra_prompt_h - to_prompt;
    }

    // ============ left column: parameter panel ============
    // No scroll container: Fl_Scroll clamps its content to its own origin, so a
    // padding offset inside it is silently dropped and the leftmost controls end
    // up flush against the viewport edge where they get clipped. The panel is
    // laid out directly instead, and the text areas below shrink with the window
    // so the column always fits without scrolling.
    const int lx = g_m.pad;
    const int lw = g_m.left_w;
    int ly = g_m.pad;

    // Every preset row is as tall as a normal control, so Save / Rename / Delete
    // are comfortably clickable rather than thin strips.
    const int preset_h = g_m.row_h;

    // top row: the engine switcher (when both generators are installed) sits
    // next to the language dropdown, so the two "which one am I talking to"
    // choices live together.
    // About sits in the top right corner, so the engine and language pickers
    // keep the left edge to themselves and only lose the room it needs.
    const int about_w = S(64);
    const int about_gap = S(10);
    if (m_btn_about)
        m_btn_about->resize(lx + lw - about_w, ly, about_w, g_m.row_h);
    const int row_x = lx;

    if (m_backend_choice) {
        // Engine names are long ("Qwen-Image-2.1"), language names are short
        // ("简体中文"), so the engine box takes the room the language box does
        // not need.
        const int bl_w = S(70);
        const int lang_lbl_w = S(62);
        const int lang_w = S(160);
        const int bc_w = std::max(S(180),
                                  lw - about_w - about_gap - bl_w - S(6) - S(16) - lang_lbl_w - lang_w);

        m_lbl_backend->resize(row_x, ly + S(9), bl_w, S(24));
        m_backend_choice->resize(row_x + bl_w + S(6), ly, bc_w, g_m.row_h);

        const int lang_x = row_x + bl_w + S(6) + bc_w + S(16);
        m_lbl_lang->resize(lang_x, ly + S(9), lang_lbl_w, S(24));
        m_lang_choice->resize(lang_x + lang_lbl_w, ly, lang_w, g_m.row_h);
    } else {
        m_lbl_lang->resize(row_x, ly + S(9), g_m.label_w - S(20), S(24));
        m_lang_choice->resize(row_x + g_m.label_w, ly, S(200), g_m.row_h);
    }
    ly += g_m.row_h + (zimage ? S(8) : S(18));

    // Z-Image's model and working mode are top-level choices - together they
    // decide which options the form passes at all - so they sit in the header,
    // right under the engine and language dropdowns.
    if (zimage) {
        const int ml_w = S(70), mm_w = S(180);
        m_lbl_zmodel->resize(lx, ly + S(9), ml_w, S(24));
        m_zmodel->resize(lx + ml_w, ly, mm_w, g_m.row_h);

        const int zx = lx + ml_w + mm_w + S(14);
        const int zl_w = S(70);
        m_lbl_zmode->resize(zx, ly + S(9), zl_w, S(24));
        m_zmode->resize(zx + zl_w, ly,
                        std::max(S(140), lx + lw - zx - zl_w), g_m.row_h);
        ly += g_m.row_h + S(10);
    }

    m_sep_top->resize(lx, ly, lw, 2);
    ly += S(18);

    // The header covers the whole parameter block, so it keeps the full width.
    m_grp_gen->resize(lx, ly, lw, S(30));
    ly += S(36);

    // prompt
    // prompt. Z-Image's -p is optional everywhere, so its heading carries no
    // "required" or "optional" at all - just the name of the option.
    if (m_lbl_prompt) {
        m_lbl_prompt->copy_label(m_backend == Backend::ZImage ? tr(Str::PromptPlain)
                                                             : tr(Str::PromptRequired));
    }
    m_lbl_prompt->resize(lx, ly, lw * 2 / 5, preset_h);
    layout_preset_row(m_prompt_presets, lx + lw * 2 / 5, ly,
                      lw - lw * 2 / 5, preset_h);
    ly += preset_h + S(6);
    m_prompt->resize(lx, ly, lw, prompt_h);
    ly += prompt_h + g_m.gap + S(6);

    // negative prompt
    m_lbl_negative->resize(lx, ly, lw * 2 / 5, preset_h);
    layout_preset_row(m_negative_presets, lx + lw * 2 / 5, ly,
                      lw - lw * 2 / 5, preset_h);
    ly += preset_h + S(6);
    m_negative->resize(lx, ly, lw, negative_h);
    ly += negative_h + g_m.gap + S(6);

    // The parameter presets sit immediately above the values they capture, not
    // up at the group header: that header covers the whole panel, while these
    // store only the generation values listed below them.
    layout_preset_row(m_param_presets, lx, ly, lw, preset_h);
    ly += preset_h + S(10);

    // CFG (-w) is a Qwen concept: there it scales the guidance. Z-Image uses -w
    // for the ControlNet strength, which sits with the image rows instead.
    if (!zimage) {
        m_cfg->resize(lx + g_m.label_w, ly, S(140), g_m.row_h);
        ly += g_m.row_h + g_m.gap;
    }

    // output folder + browse
    m_output_dir->resize(lx + g_m.label_w, ly, lw - g_m.label_w - S(92), g_m.row_h);
    m_btn_browse_output->resize(lx + lw - S(84), ly, S(84), g_m.row_h);
    ly += g_m.row_h + g_m.gap;

    // output format + base file name
    m_output_format->resize(lx + g_m.label_w, ly, S(140), g_m.row_h);
    m_output_name->resize(lx + g_m.label_w + S(250), ly,
                          lw - g_m.label_w - S(250), g_m.row_h);
    ly += g_m.row_h + g_m.gap;

    // size + preset
    m_width->resize(lx + g_m.label_w, ly, S(116), g_m.row_h);
    m_size_x->resize(lx + g_m.label_w + S(120), ly, S(24), g_m.row_h);
    m_height->resize(lx + g_m.label_w + S(150), ly, S(116), g_m.row_h);
    m_preset_menu->resize(lx + g_m.label_w + S(282), ly, S(140), g_m.row_h);
    ly += g_m.row_h + g_m.gap;

    // steps + seed
    m_steps->resize(lx + g_m.label_w, ly, S(116), g_m.row_h);
    m_seed->resize(lx + g_m.label_w + S(320), ly, S(160), g_m.row_h);
    ly += g_m.row_h + g_m.gap;

    // batch + random seed
    m_batch->resize(lx + g_m.label_w, ly, S(116), g_m.row_h);
    m_random_seed->resize(lx + g_m.label_w + S(140), ly,
                          lw - g_m.label_w - S(140), S(32));
    ly += g_m.row_h + S(18);

    // GPU
    m_gpu->resize(lx + g_m.label_w, ly, S(240), g_m.row_h);
    ly += g_m.row_h + S(18);

    // reference / input images
    m_sep_ref->resize(lx, ly, lw, 2);
    ly += S(18);

    if (zimage) {
        // Z-Image block: the image rows the chosen mode needs. Which of them
        // are visible was decided by update_zimage_rows(); the mode picker
        // itself lives up in the header.
        const bool tile = (m_zmode->value() == (int)ZMode::TileUpscale);
        // -c is a ControlNet hint for one mode and the low-res source for the
        // tile upscaler: say which one it means before measuring labels.
        if (m_control) {
            m_control->copy_label(tile ? tr(Str::ControlImageTile) : tr(Str::ControlImage));
            m_control->tooltip(tile ? tr(Str::ZTileHint) : "");
        }

        // Fl_Input paints its label to the left of the box, so this column has
        // to be as wide as the widest label in the block. A fixed width clipped
        // the longer captions or pushed them past the window edge.
        int label_w = 0;
        for (Fl_Widget *w : {static_cast<Fl_Widget *>(m_zinput),
                             static_cast<Fl_Widget *>(m_mask),
                             static_cast<Fl_Widget *>(m_outpaint),
                             static_cast<Fl_Widget *>(m_control),
                             static_cast<Fl_Widget *>(m_control_scale)}) {
            int tw = 0, th = 0;
            w->measure_label(tw, th);
            label_w = std::max(label_w, tw);
        }
        label_w += S(8);
        if (lx + label_w > lx + lw / 2) label_w = lw / 2;   // never eat the column

        const int bw = S(84);
        const int fx = lx + label_w;
        const int fw = std::max(S(120), lw - label_w - bw - S(8));

        // Laid out in this order; hidden rows are skipped so the block shrinks
        // to what the mode actually uses.
        struct Row { Fl_Widget *field; Fl_Button *btn; };
        const Row rows[] = {
            {m_zinput,       m_btn_browse_zinput},
            {m_mask,         m_btn_browse_mask},
            {m_outpaint,     nullptr},
            {m_control,      m_btn_browse_control},
            {m_control_scale, nullptr},
        };
        for (const Row &r : rows) {
            if (!r.field->visible()) continue;
            const int w_row = (r.field == m_control_scale) ? S(140) : fw;
            r.field->resize(fx, ly, w_row, g_m.row_h);
            if (r.btn) r.btn->resize(lx + lw - bw, ly, bw, g_m.row_h);
            ly += g_m.row_h + S(8);
        }
    } else {
        m_lbl_ref->resize(lx, ly, lw, S(30));
        ly += S(36);

        const int ref_h = std::max(S(56), std::min(S(320), H - ly - g_m.pad * 2));
        m_inputs->resize(lx, ly, lw - S(196), ref_h);
        m_btn_add_image->resize(lx + lw - S(180), ly, S(180), g_m.row_h);
        m_btn_del_image->resize(lx + lw - S(180), ly + S(46), S(180), g_m.row_h);
    }

    // ============ right column: preview + console ============
    const int rx = g_m.pad + g_m.left_w + g_m.pad;
    const int rw = W - rx - g_m.pad;
    int ry = g_m.pad;

    // preview toolbar. The buttons flow into as many lines as the column
    // needs: a narrow window wraps them instead of pushing the last ones past
    // the right edge, where they would be unreachable.
    {
        Fl_Widget *const order[] = {m_btn_gallery, m_btn_latest, m_btn_refresh,
                                    m_btn_fit, m_btn_1to1, m_btn_open_dir};
        int bx = rx, by = ry;
        for (Fl_Widget *b : order) {
            int lw_btn = 0, lh_btn = 0;
            b->measure_label(lw_btn, lh_btn);
            int bw = lw_btn + sc(30);
            if (bw < sc(56)) bw = sc(56);
            if (bx > rx && bx + bw > rx + rw) { bx = rx; by += g_m.row_h + g_m.gap; }
            b->resize(bx, by, bw, g_m.row_h);
            bx += bw + g_m.gap;
        }
        ry = by + g_m.row_h + g_m.gap;
    }

    int body = H - ry - g_m.pad - g_m.action_h - g_m.gap;
    if (body < S(300)) body = S(300);

    // The console used to take whatever the preview left over; now it takes two
    // thirds of that share, and the pictures get the rest.
    int preview_h = std::max(S(200), body * 56 / 100);
    int log_h = body - preview_h;
    log_h = std::max(S(80), log_h * 2 / 3);
    preview_h = std::max(S(200), body - log_h);

    m_gallery->resize(rx, ry, rw, preview_h);
    ry += preview_h + g_m.gap;

    m_log_view->resize(rx, ry, rw, log_h);
    ry += log_h + g_m.gap;

    // progress + actions
    const int gen_w = S(220), stop_w = S(190);
    int prog_w = rw - gen_w - stop_w - 2 * g_m.gap;
    if (prog_w < S(150)) prog_w = S(150);
    m_progress->resize(rx, ry, prog_w, g_m.action_h);
    m_btn_generate->resize(rx + rw - stop_w - gen_w - g_m.gap, ry, gen_w, g_m.action_h);
    m_btn_stop->resize(rx + rw - stop_w, ry, stop_w, g_m.action_h);

    // Where the parameter column ended. The preview column beside it always
    // reaches the bottom of the window, so only the left one can tell whether
    // the form has room to spare.
    if (bottom_out) *bottom_out = ly;
}

// --------------------------------------------------------------- i18n ----

std::string MainWindow::T(Str s) const { return std::string(tr(s)); }

void MainWindow::apply_language(Lang lang) {
    i18n_set_language(lang);

    // Keep the dropdown in step with the language actually in use: at startup
    // the config file may have overridden the environment default. Setting the
    // value programmatically does not fire the widget callback.
    if (m_lang_choice) m_lang_choice->value(lang == Lang::ChineseSimplified ? 1 : 0);

    // Window title is handled by the caller (main) and here for runtime switches.
    // The version rides along so it is visible in screenshots too.
    {
        const char *base = (m_backend == Backend::ZImage) ? tr(Str::AppTitleZImage)
                                                          : tr(Str::AppTitle);
        const std::string title = std::string(base) + "  v" + kAppVersion;
        copy_label(title.c_str());
    }

    // Labels attached to inputs / choices are drawn by the widget itself.
    if (m_lbl_cfg)     m_cfg->copy_label(tr(Str::Cfg));
    if (m_lbl_output_dir)    m_output_dir->copy_label(tr(Str::OutputDir));
    if (m_lbl_output_name)   m_output_name->copy_label(tr(Str::OutputName));
    if (m_lbl_output_format) m_output_format->copy_label(tr(Str::OutputFormat));
    if (m_lbl_size)    m_width->copy_label(tr(Str::Size));
    if (m_lbl_steps)   m_steps->copy_label(tr(Str::Steps));
    if (m_lbl_seed)    m_seed->copy_label(tr(Str::Seed));
    if (m_lbl_batch)   m_batch->copy_label(tr(Str::Batch));
    if (m_lbl_gpu)     m_gpu->copy_label(tr(Str::Gpu));

    // Standalone boxes.
    if (m_lbl_lang)     m_lbl_lang->copy_label(tr(Str::Language));
    if (m_lbl_backend)  m_lbl_backend->copy_label(tr(Str::Backend));
    if (m_grp_gen)      m_grp_gen->copy_label(tr(Str::GroupGeneration));
    if (m_lbl_prompt) { m_lbl_prompt->copy_label(tr(Str::PromptRequired)); }
    if (m_lbl_negative) m_lbl_negative->copy_label(tr(Str::NegativeOptional));
    if (m_lbl_ref)      m_lbl_ref->copy_label(m_backend == Backend::ZImage
                                              ? tr(Str::RefImagesZImage)
                                              : tr(Str::RefImages));

    // Z-Image only controls. The -c caption depends on the mode, and
    // layout_widgets() (which runs right after) is what settles it.
    if (m_lbl_zmode) m_lbl_zmode->copy_label(tr(Str::ZMode));
    if (m_lbl_zmodel) m_lbl_zmodel->copy_label(tr(Str::ZModel));
    if (m_zmodel) {
        const int keep = m_zmodel->value();
        m_zmodel->clear();
        m_zmodel->add(kZModelTurbo);   // 0
        m_zmodel->add(kZModelBase);    // 1
        m_zmodel->value(keep);
    }
    if (m_zmode) {
        const int keep = m_zmode->value();
        m_zmode->clear();
        m_zmode->add(tr(Str::ZModeText));      // 0
        m_zmode->add(tr(Str::ZModeInpaint));   // 1
        m_zmode->add(tr(Str::ZModeOutpaint));  // 2
        m_zmode->add(tr(Str::ZModeControl));   // 3
        m_zmode->add(tr(Str::ZModeTile));      // 4
        m_zmode->value(keep);
    }
    if (m_zinput)            m_zinput->copy_label(tr(Str::ZImageInput));
    if (m_lbl_mask)          m_mask->copy_label(tr(Str::MaskImage));
    if (m_mask)              m_mask->tooltip(tr(Str::MaskHint));
    if (m_lbl_control)       m_control->copy_label(tr(Str::ControlImage));
    if (m_lbl_control_scale) m_control_scale->copy_label(tr(Str::ControlScale));
    if (m_lbl_outpaint)      m_outpaint->copy_label(tr(Str::Outpaint));
    if (m_outpaint)          m_outpaint->tooltip(tr(Str::OutpaintHint));

    // Buttons.
    if (m_preset_menu)        m_preset_menu->copy_label(tr(Str::Preset));
    if (m_btn_browse_output)  m_btn_browse_output->copy_label(tr(Str::Browse));
    if (m_btn_add_image)      m_btn_add_image->copy_label(tr(Str::AddImage));
    if (m_btn_del_image)      m_btn_del_image->copy_label(tr(Str::RemoveSelected));
    if (m_btn_browse_mask)    m_btn_browse_mask->copy_label(tr(Str::Browse));
    if (m_btn_browse_control) m_btn_browse_control->copy_label(tr(Str::Browse));
    if (m_btn_browse_zinput)  m_btn_browse_zinput->copy_label(tr(Str::Browse));
    if (m_btn_gallery)        m_btn_gallery->copy_label(tr(Str::GalleryBack));
    if (m_btn_latest)         m_btn_latest->copy_label(tr(Str::PreviewLatest));
    if (m_btn_refresh)        m_btn_refresh->copy_label(tr(Str::Reload));
    if (m_btn_open_dir)       m_btn_open_dir->copy_label(tr(Str::OpenOutputDir));
    if (m_btn_fit)            m_btn_fit->copy_label(tr(Str::Fit));
    if (m_btn_about)          m_btn_about->copy_label(tr(Str::About));
    if (m_btn_1to1)           m_btn_1to1->copy_label(tr(Str::Zoom1to1));
    if (m_btn_generate)       m_btn_generate->copy_label(tr(Str::Generate));
    if (m_btn_stop)           m_btn_stop->copy_label(tr(Str::Stop));
    if (m_random_seed)        m_random_seed->copy_label(tr(Str::RandomSeed));

    // Preset rows: button captions and the dropdown placeholder.
    for (PresetRow *row : {&m_prompt_presets, &m_negative_presets, &m_param_presets}) {
        if (!row->choice) continue;
        row->save->copy_label(tr(Str::PresetSave));
        row->rename->copy_label(tr(Str::PresetRename));
        row->del->copy_label(tr(Str::PresetDelete));
        const int keep = row->choice->value();
        row->rebuild_choice();      // re-adds the placeholder in the new language
        row->choice->value(keep);   // rebuild resets the selection; keep it
    }

    layout_widgets();   // labels changed width -> relayout
    update_param_hints();   // the placeholders are translated too
    update_view_buttons();
    redraw();
}

// --------------------------------------------------------------- engine ---

// The values each engine starts from. They are compiled in on purpose: they
// describe how the generator behaves out of the box, not a user preference.
const MainWindow::Params &MainWindow::default_params(Backend b) {
    static const Params qwen = [] {
        Params p;
        p.cfg = kDefCfg;         // -w is the CFG scale
        p.width = kDefWidth;
        p.height = kDefHeight;
        p.steps = kDefSteps;     // -l default=40
        p.seed = kDefSeed;       // -r default=42
        p.batch = kDefBatch;
        p.random_seed = false;
        p.gpu = "auto";
        return p;
    }();
    static const Params zimage = [] {
        Params p;
        // zimage-ncnn-vulkan documents -l default=auto and -r default=rand, so
        // the form starts with an empty steps field ("leave -l out") and the
        // random-seed box ticked. Qwen's 40/42 must not leak over here.
        p.steps = "";
        p.seed = kDefSeed;
        p.random_seed = true;
        return p;
    }();
    return b == Backend::ZImage ? zimage : qwen;
}

void MainWindow::store_params(Backend b) {
    Params &p = m_params[(int)b];
    // Only values that make sense are recorded. A half-typed or plainly wrong
    // entry stays in its field - so it is not lost while being corrected - but
    // never reaches the config file, where it would come back on the next
    // start. The last good value is kept instead.
    if (number_ok(m_cfg->value(), 0))        p.cfg = m_cfg->value();
    if (int_ok(m_width->value(), 1))         p.width = m_width->value();
    if (int_ok(m_height->value(), 1))        p.height = m_height->value();
    {
        const char *s = m_steps->value();
        if (s && (*s == 0 || int_ok(s, 1))) p.steps = s;   // empty means auto
    }
    if (int_ok(m_seed->value(), LLONG_MIN))  p.seed = m_seed->value();
    if (int_ok(m_batch->value(), 1))         p.batch = m_batch->value();
    p.random_seed = m_random_seed->value() != 0;
    // The dropdown can only hold entries we put there.
    const char *gpu = m_gpu->text();
    p.gpu = (gpu && *gpu) ? gpu : "auto";
}

void MainWindow::load_params(Backend b) {
    const Params &p = m_params[(int)b];
    m_cfg->value(p.cfg.c_str());
    m_width->value(p.width.c_str());
    m_height->value(p.height.c_str());
    m_steps->value(p.steps.c_str());
    m_seed->value(p.seed.c_str());
    m_batch->value(p.batch.c_str());
    m_random_seed->value(p.random_seed ? 1 : 0);
    // Match the GPU entry by text: the list order is fixed, but storing the
    // label keeps the config file readable.
    for (int i = 0; i < m_gpu->size(); ++i) {
        const char *t = m_gpu->text(i);
        if (t && p.gpu == t) { m_gpu->value(i); break; }
    }
}

void MainWindow::set_backend(Backend b) {
    if (b == m_backend) return;

    // Park the values that belong to the engine being left, then bring back the
    // ones the new engine was last used with.
    store_params(m_backend);

    m_backend = b;
    m_exe_path   = m_app_dir + "/" + backend_spec(b).exe_name;
    update_model_path();
    load_params(b);

    if (m_backend_choice)
        m_backend_choice->value(b == Backend::ZImage ? 1 : 0);

    update_backend_ui();

    if (!file_exists(m_exe_path))
        log("[error] generator not found: " + m_exe_path);

    save_settings();     // the choice survives a restart
    refresh_gallery();
}

void MainWindow::update_backend_ui() {
    const bool zimage = (m_backend == Backend::ZImage);

    auto set = [](Fl_Widget *w, bool on) {
        if (!w) return;
        if (on) w->show();
        else    w->hide();
    };

    // The mode picker and the single-image inputs belong to Z-Image; the
    // reference list belongs to Qwen. Both keep their own values, so switching
    // engines never mixes the two sets up.
    set(m_lbl_zmode, zimage);
    set(m_zmode, zimage);
    set(m_lbl_zmodel, zimage);
    set(m_zmodel, zimage);
    set(m_cfg, !zimage);          // -w means CFG for Qwen only
    set(m_inputs, !zimage);
    set(m_btn_add_image, !zimage);
    set(m_btn_del_image, !zimage);
    set(m_lbl_ref, !zimage);

    update_zimage_rows();
    update_param_hints();

    // Retranslate first (the reference heading and the window title depend on
    // the engine), then lay out for the new set of visible rows. Rebuilding the
    // mode list resets its item states, so the model gate is applied afterwards.
    apply_language(i18n_language());
    update_zmodel_modes();

    // The Z-Image form can be taller, so the window must be allowed to grow a
    // little further before the layout would run out of room.
    // The size_range call also needs the width that keeps four columns: this
    // runs on every engine switch and would otherwise restore the old floor.
    size_range(grid_min_width(), zimage ? g_m.row_h * 22 : g_m.row_h * 20);
}

void MainWindow::update_param_hints() {
    // Placeholder text for every input: what to type, or - for the numeric
    // options - what the generator falls back to when the box is left empty.
    // Those defaults are the ones the two manuals list ("-l ... (default=40)"
    // against "(default=auto)", "-r ... (default=42)" against "(default=rand)",
    // ...). None of it ever becomes a value.
    const bool z = (m_backend == Backend::ZImage);
    const ZMode mode = m_zmode ? (ZMode)m_zmode->value() : ZMode::TextToImage;

    if (m_prompt)   m_prompt->hint(tr(Str::HintPrompt));
    if (m_negative) m_negative->hint(tr(Str::HintNegative));

    m_output_dir->hint(tr(Str::HintOutputDir));
    m_output_name->hint(tr(Str::HintOutputName));

    m_cfg->hint("1.0");                 // -w, qwen: true CFG scale
    m_width->hint("1024");              // -s
    m_height->hint("1024");
    m_steps->hint(z ? "auto" : "40");  // -l
    m_seed->hint(z ? "rand" : "42");   // -r
    m_batch->hint("1");                 // -b

    if (m_inputs)        m_inputs->hint(tr(Str::HintRefImages));
    if (m_control_scale) m_control_scale->hint("1.0");           // -w, zimage
    if (m_outpaint)      m_outpaint->hint(tr(Str::HintOutpaint)); // -x

    // The image slots say what that particular slot wants, which depends on
    // the mode the form is in. The tooltips add the size rules the generator
    // enforces but the box itself cannot express.
    if (m_zinput) {
        const bool outpaint = (mode == ZMode::Outpaint);
        m_zinput->hint(outpaint ? tr(Str::HintZInputOutpaint) : tr(Str::HintZInputInpaint));
        m_zinput->tooltip(outpaint ? tr(Str::HintOutpaintSize) : tr(Str::HintInpaintSize));
    }
    if (m_mask) m_mask->hint(tr(Str::HintMask));         // -k
    if (m_control) {                                     // -c
        const bool tile = (mode == ZMode::TileUpscale);
        m_control->hint(tile ? tr(Str::HintLowRes) : tr(Str::HintControl));
        m_control->tooltip(tile ? tr(Str::ZTileHint) : tr(Str::HintControlSize));
    }
}

void MainWindow::update_zimage_rows() {
    const bool zimage = (m_backend == Backend::ZImage);
    const ZMode mode = m_zmode ? (ZMode)m_zmode->value() : ZMode::TextToImage;
    // Per the tool's own usage: inpaint takes -i with -k, outpaint -i with -x,
    // ControlNet -c with -w, and the tile upscaler -c (a low-resolution image)
    // with -t. Only the rows a mode actually passes on are shown.
    const bool needs_input    = zimage && (mode == ZMode::Inpaint || mode == ZMode::Outpaint);
    const bool needs_mask     = zimage && mode == ZMode::Inpaint;
    const bool needs_outpaint = zimage && mode == ZMode::Outpaint;
    const bool needs_control  = zimage && (mode == ZMode::ControlNet ||
                                           mode == ZMode::TileUpscale);

    auto set = [](Fl_Widget *w, bool on) {
        if (!w) return;
        if (on) w->show();
        else    w->hide();
    };
    set(m_zinput, needs_input);
    set(m_btn_browse_zinput, needs_input);
    set(m_mask, needs_mask);
    set(m_btn_browse_mask, needs_mask);
    set(m_outpaint, needs_outpaint);
    set(m_control, needs_control);
    set(m_btn_browse_control, needs_control);
    set(m_control_scale, needs_control);

    // Entering or leaving the outpaint mode changes whether the size is ours to
    // compute, so keep the lock in step.
    update_outpaint_size();
}

// In outpaint mode the output size is not a free choice: the generator insists
// on the input picture plus the margins ("image-size must match outpaint canvas
// size"). Fill it in and lock the two boxes, so the value cannot drift away
// from what the run will need.
//
// While that cannot be worked out - no picture, an unreadable one, margins that
// are missing or malformed - the fields stay blank and locked too. Handing them
// back to the user here would only invite a number that cannot be right, and
// the reason is on the tooltip.
void MainWindow::update_outpaint_size() {
    const bool zimage = (m_backend == Backend::ZImage);
    const ZMode mode = m_zmode ? (ZMode)m_zmode->value() : ZMode::TextToImage;

    auto set_state = [this](bool locked, const char *tip) {
        if (locked) {
            if (m_width->active()) { m_width->deactivate(); m_height->deactivate(); }
        } else {
            if (!m_width->active()) { m_width->activate(); m_height->activate(); }
        }
        m_width->tooltip(tip ? tip : "");
        m_height->tooltip(tip ? tip : "");
        m_width->redraw();
        m_height->redraw();
    };

    if (!zimage || mode != ZMode::Outpaint) {
        set_state(false, nullptr);
        return;
    }

    int iw = 0, ih = 0, l = 0, t = 0, r = 0, b = 0;
    const char *src = m_zinput->value();
    if (!src || !*src || !image_dimensions(src, iw, ih) ||
        sscanf(m_outpaint->value(), "%d,%d,%d,%d", &l, &t, &r, &b) != 4 ||
        l < 0 || t < 0 || r < 0 || b < 0) {
        if (m_width->value()[0]) m_width->value("");
        if (m_height->value()[0]) m_height->value("");
        set_state(true, tr(Str::HintOutpaintSize));
        return;
    }

    const std::string w = std::to_string(iw + l + r);
    const std::string h = std::to_string(ih + t + b);
    if (m_width->value() != w) m_width->value(w.c_str());
    if (m_height->value() != h) m_height->value(h.c_str());
    set_state(true, tr(Str::SizeAutoComputed));
}

void MainWindow::update_zmodel_modes() {
    if (!m_zmodel || !m_zmode) return;

    // ControlNet and the tile upscaler it drives need z-image-turbo's weights;
    // with the plain z-image model those two entries are greyed out rather than
    // hidden, so it stays visible that they exist and what they require.
    // Called again whenever the mode list is rebuilt (a language change does
    // that), because rebuilding resets the item states.
    const bool turbo = (m_zmodel->value() == (int)ZModel::Turbo);
    Fl_Menu_Item *items = const_cast<Fl_Menu_Item *>(m_zmode->menu());
    for (int i = 0; items && i < m_zmode->size(); ++i) {
        const bool needs_turbo = (i == (int)ZMode::ControlNet ||
                                  i == (int)ZMode::TileUpscale);
        if (needs_turbo && !turbo) items[i].deactivate();
        else                       items[i].activate();
    }
}

void MainWindow::update_model_path() {
    if (m_backend == Backend::ZImage && m_zmodel)
        m_model_path = resolve_model_path(m_app_dir, Backend::ZImage,
                                          zmodel_dir((ZModel)m_zmodel->value()));
    else
        m_model_path = resolve_model_path(m_app_dir, m_backend);
}

void MainWindow::update_view_buttons() {
    const bool detail = m_gallery && m_gallery->mode() == GalleryView::Mode::Detail;
    const bool has    = m_gallery && m_gallery->count() > 0;

    // "Grid" only means something while a picture fills the pane, and zooming
    // only applies to that picture: in the grid both are dead controls, and
    // leaving them clickable would make the toolbar lie about what it does.
    if (m_btn_gallery) {
        if (detail) m_btn_gallery->activate();
        else        m_btn_gallery->deactivate();
    }
    for (Fl_Button *b : {m_btn_fit, m_btn_1to1}) {
        if (!b) continue;
        if (detail) b->activate();
        else        b->deactivate();
    }
    if (m_btn_latest) {
        if (has) m_btn_latest->activate();
        else     m_btn_latest->deactivate();
    }
}

void MainWindow::refresh_gallery() {
    if (!m_gallery) return;

    m_gallery->set_directory(output_dir());
    m_gallery->refresh();
    update_view_buttons();

    if (!m_gallery->scan_error().empty())
        log(m_gallery->scan_error());
}

void MainWindow::pick_image_into(Fl_Input *field, Str title) {
    if (!field) return;

    std::string start = m_settings->get("last_input_dir", "");
    if (start.empty()) {
        const char *home = getenv("HOME");
        start = (home && *home) ? home : "/";
    }

    std::string last;
    const std::vector<std::string> files =
        Picker::choose_files(tr(title), start, "*.{png,jpg,jpeg,webp,bmp}", false,
                             tr(Str::PickerConfirmSel), &last);
    if (!last.empty()) {
        m_settings->set("last_input_dir", last);
        save_settings();
    }
    if (!files.empty()) field->value(files[0].c_str());
}

// ------------------------------------------------------------------ log ---

void MainWindow::log(const std::string &s) {
    if (!m_log_buf) return;
    m_log_buf->append((s + "\n").c_str());
    // The style buffer has to stay exactly as long as the text, so every append
    // here is mirrored there.
    if (m_log_style)
        m_log_style->append(std::string(s.size() + 1,
                                        is_problem_line(s) ? 'B' : 'A').c_str());
    // scroll to end
    m_log_view->insert_position(m_log_buf->length());
    m_log_view->show_insert_position();
    // Mirror to the terminal so a failure can be diagnosed from a shell too.
    fputs((s + "\n").c_str(), stderr);
    fflush(stderr);
}

void MainWindow::append_log(const std::string &line) {
    // NOTE: called from the FLTK main thread only
    log(line);
}

// ------------------------------------------------------------ validation ---

bool MainWindow::validate(GenOptions &opt, std::string &err) {
    opt.backend = m_backend;
    opt.prompt = buffer_text(m_prompt_buf);
    // zimage-ncnn-vulkan accepts a missing -p in every mode (it falls back to a
    // random prompt), so the Z-Image form never insists on one. Qwen has no
    // such fallback and still requires it.
    if (opt.prompt.empty() && m_backend != Backend::ZImage) {
        err = tr(Str::ErrPromptRequired); return false;
    }

    opt.negative_prompt = buffer_text(m_negative_buf);

    double cfg = 1.0;
    if (sscanf(m_cfg->value(), "%lf", &cfg) != 1 || cfg < 0) {
        err = tr(Str::ErrCfg); return false;
    }
    opt.cfg_scale = cfg;

    // The ControlNet scale is only meaningful in the modes that use -c, and an
    // empty field means "leave it at the default".
    double cscale = 1.0;
    {
        const char *csv = m_control_scale->value();
        if (csv && *csv) {
            if (sscanf(csv, "%lf", &cscale) != 1 || cscale < 0) {
                err = tr(Str::ErrControlScale); return false;
            }
        }
    }
    opt.control_scale = cscale;

    opt.output_path = output_path();
    if (opt.output_path.empty()) { err = tr(Str::ErrOutput); return false; }

    // The model location is fixed: it sits next to this executable.
    opt.model_path = m_model_path;

    // Z-Image's modes decide which options exist at all, so they are checked
    // before the size is: missing margins should be reported as missing
    // margins, not as an unusable size further down.
    const ZMode zmode = m_zmode ? (ZMode)m_zmode->value() : ZMode::TextToImage;
    const bool outpaint_mode = (m_backend == Backend::ZImage) && zmode == ZMode::Outpaint;
    if (m_backend == Backend::ZImage) {
        // Each mode passes its own subset; start from nothing and add what the
        // chosen one needs.
        opt.mask_path.clear();
        opt.control_path.clear();
        opt.outpaint.clear();
        opt.tile_upscale = false;

        if (zmode == ZMode::Inpaint) {
            if (opt.inputs.empty()) { err = tr(Str::ErrZInputNeeded); return false; }
            opt.mask_path = m_mask->value();
            if (opt.mask_path.empty()) { err = tr(Str::ErrZMaskNeeded); return false; }
        } else if (zmode == ZMode::Outpaint) {
            if (opt.inputs.empty()) { err = tr(Str::ErrZInputNeeded); return false; }
            opt.outpaint = m_outpaint->value();
            if (opt.outpaint.empty()) { err = tr(Str::ErrZOutpaintNeeded); return false; }
            int l = 0, t = 0, r = 0, b = 0;
            char extra = 0;
            if (sscanf(opt.outpaint.c_str(), "%d,%d,%d,%d%c", &l, &t, &r, &b, &extra) != 4 ||
                l < 0 || t < 0 || r < 0 || b < 0) {
                err = tr(Str::ErrOutpaintFormat); return false;
            }
        } else if (zmode == ZMode::ControlNet || zmode == ZMode::TileUpscale) {
            opt.control_path = m_control->value();
            if (opt.control_path.empty()) { err = tr(Str::ErrZControlNeeded); return false; }
            opt.tile_upscale = (zmode == ZMode::TileUpscale);
        }
    }

    int w = atoi(m_width->value());
    int h = atoi(m_height->value());
    if (w <= 0 || h <= 0) { err = tr(Str::ErrSizePositive); return false; }
    // Qwen image editing needs multiples of 32, plain text-to-image 16.
    // Z-Image has a single size rule for every mode, and 16 is the safe one.
    int mult = 16;
    const char *mode = tr(Str::TextToImage);
    if (m_backend == Backend::Qwen && !opt.inputs.empty()) {
        mult = 32;
        mode = tr(Str::ImageEditing);
    }
    // Where the size comes from a picture that the generator expands, the
    // usual alignment rules do not apply. Verified against the binary: an
    // outpaint canvas of 656x556 (400x300 picture plus 128 margins) runs fine,
    // while inpaint with -s 400,300 and ControlNet with -s 250,250 are both
    // refused with "width and height must be multiple of 16".
    if (!outpaint_mode) {
        if (w % mult || h % mult) {
            char b[256];
            snprintf(b, sizeof(b), tr(Str::ErrSizeMultiple), mult, mode, w, h);
            err = b; return false;
        }
        // zimage-ncnn-vulkan rejects small sizes on top of the multiple-of-16
        // rule: checked against the binary, 64x64 fails with "(width / 16) *
        // (height / 16) must be >= 32".
        if (m_backend == Backend::ZImage && (w / 16) * (h / 16) < 32) {
            err = tr(Str::ErrSizeTooSmall); return false;
        }
    }

    // The 2048 cap holds for every mode, the outpaint canvas included - that is
    // where it usually bites, because the canvas is the picture plus the
    // margins. Checked against the binary: 4096x4096 fails with "width and
    // height must be <= 2048". Qwen has no such cap (it runs out of memory
    // instead, which is the machine's limit, not the generator's rule).
    if (m_backend == Backend::ZImage) {
        const int kMaxSide = 2048;
        if (w > kMaxSide || h > kMaxSide) {
            char b[384];
            // In outpaint mode the number came from the picture plus the
            // margins, so say so: "too large" alone leaves the user guessing
            // which of the two to change.
            if (outpaint_mode)
                snprintf(b, sizeof(b), tr(Str::ErrOutpaintTooLarge), w, h,
                         kMaxSide, kMaxSide);
            else
                snprintf(b, sizeof(b), tr(Str::ErrSizeTooLarge), kMaxSide, kMaxSide, w, h);
            err = b; return false;
        }
    }
    opt.width = w; opt.height = h;

    if (m_backend == Backend::ZImage) {
        // -l is optional there: an empty field means "let the model decide",
        // which is what the generator's own default does.
        const std::string steps = m_steps->value();
        opt.steps = steps.empty() ? 0 : atoi(steps.c_str());
        if (!steps.empty() && opt.steps <= 0) { err = tr(Str::ErrStepsAuto); return false; }
    } else {
        opt.steps = atoi(m_steps->value());
        if (opt.steps <= 0) { err = tr(Str::ErrSteps); return false; }
    }

    opt.batch = atoi(m_batch->value());
    if (opt.batch <= 0) { err = tr(Str::ErrBatch); return false; }

    long long seed = 42;
    if (sscanf(m_seed->value(), "%lld", &seed) != 1) seed = 42;
    if (m_random_seed->value()) {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<long long> dist(0, 4294967295LL);
        seed = dist(gen);
        m_seed->value(std::to_string(seed).c_str());
    }
    opt.seed = seed;

    switch (m_gpu->value()) {
        case 0: opt.gpu_id = INT_MAX; break;   // auto (omit -g)
        case 1: opt.gpu_id = -1; break;        // cpu
        default: opt.gpu_id = m_gpu->value() - 2; break; // 0,1,2,3
    }

    if (m_backend != Backend::ZImage && opt.inputs.size() > 10) {
        err = tr(Str::ErrTooManyImages); return false;
    }
    return true;
}

GenOptions MainWindow::collect_options() {
    GenOptions o;
    o.backend = m_backend;
    o.prompt = buffer_text(m_prompt_buf);
    o.negative_prompt = buffer_text(m_negative_buf);
    sscanf(m_cfg->value(), "%lf", &o.cfg_scale);
    sscanf(m_control_scale->value(), "%lf", &o.control_scale);
    o.output_path = output_path();
    o.width = atoi(m_width->value());
    o.height = atoi(m_height->value());
    o.steps = atoi(m_steps->value());
    o.seed = atoll(m_seed->value());
    o.model_path = m_model_path;
    o.batch = atoi(m_batch->value());

    if (m_backend == Backend::ZImage) {
        // validate() keeps only the parts the chosen mode really passes on.
        o.mask_path = m_mask->value();
        o.control_path = m_control->value();
        o.outpaint = m_outpaint->value();
        const ZMode mode = m_zmode ? (ZMode)m_zmode->value() : ZMode::TextToImage;
        o.tile_upscale = (mode == ZMode::TileUpscale);
        if ((mode == ZMode::Inpaint || mode == ZMode::Outpaint) && m_zinput->value()[0])
            o.inputs.push_back(m_zinput->value());
        return o;
    }

    // Qwen: the reference list, capped the way the generator documents it.
    for (int i = 1; i <= m_inputs->size(); ++i)
        o.inputs.push_back(m_inputs->text(i));
    return o;
}

// -------------------------------------------------------------- browsing ---

void MainWindow::pick_input_images() {
    // Where to start: the folder last browsed here, else the folder an existing
    // reference image came from, else home.
    std::string start = m_settings->get("last_input_dir", "");
    if (start.empty()) {
        const char *home = getenv("HOME");
        start = (home && *home) ? home : "/";
        if (m_inputs->size() > 0) {
            const std::string first = m_inputs->text(1);
            const size_t slash = first.find_last_of('/');
            if (slash != std::string::npos) start = first.substr(0, slash);
        }
    }

    std::string last;
    const std::vector<std::string> files =
        Picker::choose_files(tr(Str::SelectRefImages), start,
                             "*.{png,jpg,jpeg,webp}", true,
                             tr(Str::PickerConfirmSel), &last);

    if (!last.empty()) {
        m_settings->set("last_input_dir", last);
        save_settings();       // remember it right away, not only on exit
    }

    // Skip anything already in the list, so picking the same file twice does not
    // create a duplicate entry.
    std::set<std::string> existing;
    for (int i = 1; i <= m_inputs->size(); ++i)
        existing.insert(m_inputs->text(i));

    // The engines differ in how many images -i accepts: ten references for
    // Qwen, a single LanPaint input for Z-Image.
    const int limit = backend_spec(m_backend).max_inputs;
    const Str full = (m_backend == Backend::ZImage) ? Str::MaxRefImagesZImage
                                                    : Str::MaxRefImages;
    for (const std::string &f : files) {
        if (m_inputs->size() >= limit) { log(tr(full)); break; }
        if (!existing.insert(f).second) continue;   // already listed
        m_inputs->add(f.c_str());
    }
}

void MainWindow::remove_selected_input() {
    int sel = m_inputs->value();
    if (sel > 0) {
        // Fl_Browser::remove(int) removes the 1-based line
        m_inputs->remove(sel);
    }
}

void MainWindow::pick_output_dir() {
    // Start from the remembered browse folder, which is the *parent* of the
    // last choice (see Picker::choose_directory). Starting from the current
    // output folder instead would drop the user straight back inside it, where
    // no sibling folder can be picked.
    std::string start = m_settings->get("last_output_dir", "");
    if (start.empty()) start = m_output_dir->value();
    if (start.empty()) start = m_app_dir;

    std::string last;
    const std::string dir = Picker::choose_directory(tr(Str::OutputImage), start,
                                                     tr(Str::PickerChooseDir), &last);
    if (!last.empty()) {
        m_settings->set("last_output_dir", last);
        save_settings();
    }
    if (!dir.empty()) {
        m_output_dir->value(dir.c_str());
        refresh_gallery();   // the grid follows the folder it points at
    }
}

// ------------------------------------------------------------- settings ---

std::string MainWindow::output_dir() const {
    std::string dir = m_output_dir->value();
    if (dir.empty()) dir = m_app_dir;
    while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
    return dir;
}

std::string MainWindow::output_path() const {
    const std::string dir = output_dir();

    std::string name = m_output_name->value();
    if (name.empty()) name = "out";
    // The format box decides the extension, so drop anything the user typed.
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);

    const char *fmt = m_output_format->text();
    return dir + "/" + name + "." + ((fmt && *fmt) ? fmt : "png");
}

// True when the generator would overwrite something already on disk. Both
// shapes it writes are checked: "name.ext" for a single image, and
// "name-0.ext" upward when a run produces several.
static bool output_name_taken(const std::string &dir, const std::string &stem,
                              const std::string &ext) {
    struct stat st;
    if (stat((dir + "/" + stem + "." + ext).c_str(), &st) == 0) return true;
    return stat((dir + "/" + stem + "-0." + ext).c_str(), &st) == 0;
}

// Same path as output_path(), except that a name already in use is bumped to
// _1, _2, ... so an earlier picture is never silently replaced.
std::string MainWindow::unique_output_path() const {
    const std::string want = output_path();
    const size_t slash = want.find_last_of('/');
    const size_t dot = want.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return want;   // nothing that looks like an extension: leave it alone

    const size_t stem_at = (slash == std::string::npos) ? 0 : slash + 1;
    const std::string dir  = (slash == std::string::npos) ? "." : want.substr(0, slash);
    const std::string stem = want.substr(stem_at, dot - stem_at);
    const std::string ext  = want.substr(dot + 1);

    std::string probe = stem;
    for (int i = 1; i < 10000 && output_name_taken(dir, probe, ext); ++i)
        probe = stem + "_" + std::to_string(i);

    if (probe == stem) return want;
    return dir + "/" + probe + "." + ext;
}

void MainWindow::load_settings() {
    Settings &s = *m_settings;   // the file was already read in the constructor

    // A stored language beats the environment default picked at startup.
    const std::string lang = s.get("language", "");
    if (lang == "zh") {
        i18n_set_language(Lang::ChineseSimplified);
        m_lang_choice->value(1);
    } else if (lang == "en") {
        i18n_set_language(Lang::English);
        m_lang_choice->value(0);
    }

    m_prompt_buf->text(s.get("prompt", "").c_str());
    m_negative_buf->text(s.get("negative", "").c_str());

    // Each engine keeps its own copy of the shared parameters. Qwen uses the
    // historical unprefixed keys, Z-Image gets a z_ prefix, and a missing key
    // falls back to that engine's own defaults - which is where the different
    // -l (40 vs auto) and -r (42 vs rand) behaviour lives.
    for (Backend b : {Backend::Qwen, Backend::ZImage}) {
        const std::string pre = (b == Backend::ZImage) ? "z_" : "";
        const Params &d = default_params(b);
        Params &p = m_params[(int)b];
        p.cfg    = s.get(pre + "cfg", d.cfg);
        p.width  = s.get(pre + "width", d.width);
        p.height = s.get(pre + "height", d.height);
        p.steps  = s.get(pre + "steps", d.steps);
        p.seed   = s.get(pre + "seed", d.seed);
        p.batch  = s.get(pre + "batch", d.batch);
        p.random_seed = s.get_bool(pre + "random_seed", d.random_seed);
        p.gpu    = s.get(pre + "gpu", d.gpu);
    }
    load_params(m_backend);

    m_output_dir->value(s.get("output_dir", m_app_dir).c_str());
    m_output_name->value(s.get("output_name", "out").c_str());

    const std::string fmt = s.get("output_format", "png");
    int fi = 0;
    for (int i = 0; i < m_output_format->size(); ++i) {
        const char *t = m_output_format->text(i);
        if (t && fmt == t) { fi = i; break; }
    }
    m_output_format->value(fi);

    const int n = s.get_int("input_count", 0);
    for (int i = 0; i < n && i < 10; ++i) {
        const std::string path = s.get("input_" + std::to_string(i), "");
        if (!path.empty()) m_inputs->add(path.c_str());
    }

    // Z-Image: the single input image slot and the working mode.
    m_zinput->value(s.get("zimage_input", "").c_str());
    m_mask->value(s.get("mask", "").c_str());
    m_control->value(s.get("control", "").c_str());
    m_control_scale->value(s.get("control_scale", "1.0").c_str());
    m_outpaint->value(s.get("outpaint", kDefOutpaint).c_str());
    if (m_zmode) m_zmode->value((int)zmode_from_key(s.get("zimage_mode", "text")));
    if (m_zmodel) m_zmodel->value((int)zmodel_from_key(s.get("zimage_model", "turbo")));
    update_model_path();   // -m follows the stored model choice

    // A stored pair that no longer makes sense (the base model with a mode that
    // needs turbo weights) falls back to text-to-image.
    if (m_zmodel && m_zmodel->value() == (int)ZModel::Base && m_zmode) {
        const ZMode mode = (ZMode)m_zmode->value();
        if (mode == ZMode::ControlNet || mode == ZMode::TileUpscale)
            m_zmode->value((int)ZMode::TextToImage);
    }

    load_preset_rows();
    update_backend_ui();   // show the rows the stored engine needs
}

void MainWindow::save_settings() {
    if (!m_settings) return;
    Settings &s = *m_settings;

    // Which build wrote this file.
    s.set("version", kAppVersion);

    s.set("language", i18n_language() == Lang::ChineseSimplified ? "zh" : "en");
    s.set("prompt", buffer_text(m_prompt_buf));
    s.set("negative", buffer_text(m_negative_buf));

    // The controls hold the active engine's values; park them before writing
    // both engines' sets out.
    store_params(m_backend);
    for (Backend b : {Backend::Qwen, Backend::ZImage}) {
        const std::string pre = (b == Backend::ZImage) ? "z_" : "";
        const Params &p = m_params[(int)b];
        s.set(pre + "cfg", p.cfg);
        s.set(pre + "width", p.width);
        s.set(pre + "height", p.height);
        s.set(pre + "steps", p.steps);
        s.set(pre + "seed", p.seed);
        s.set(pre + "batch", p.batch);
        s.set_bool(pre + "random_seed", p.random_seed);
        s.set(pre + "gpu", p.gpu);
    }

    s.set("output_dir", m_output_dir->value());
    s.set("output_name", m_output_name->value());
    const char *fmt = m_output_format->text();
    s.set("output_format", fmt ? fmt : "png");

    const int n = m_inputs->size();
    s.set_int("input_count", n);
    for (int i = 1; i <= n; ++i)
        s.set("input_" + std::to_string(i - 1), m_inputs->text(i));

    // The engine choice and everything only that engine understands.
    s.set("backend", m_backend == Backend::ZImage ? "zimage" : "qwen");
    s.set("zimage_input", m_zinput->value());
    s.set("zimage_mode", zmode_key(m_zmode ? (ZMode)m_zmode->value() : ZMode::TextToImage));
    s.set("zimage_model", (m_zmodel && m_zmodel->value() == (int)ZModel::Base) ? "base" : "turbo");
    s.set("mask", m_mask->value());
    s.set("control", m_control->value());
    if (number_ok(m_control_scale->value(), 0))
        s.set("control_scale", m_control_scale->value());
    if (outpaint_ok(m_outpaint->value()))
        s.set("outpaint", m_outpaint->value());

    // Presets go into the same store, so they must be written before save().
    store_preset_rows();

    if (!s.save())
        log(std::string("[error] ") + s.error());
}

// ------------------------------------------------------------ generation ---

void MainWindow::set_running(bool running) {
    m_running = running;
    if (running) {
        m_btn_generate->deactivate();
        m_btn_stop->activate();
        m_progress->value(0);
        m_progress->copy_label(tr(Str::Running));
    } else {
        Fl::remove_timeout(cb_run_tick, this);   // stop the clock
        m_btn_generate->activate();
        m_btn_stop->deactivate();
    }
}

// " 01:23" - appended to whatever the progress line is saying, so the run's
// duration is visible while it runs and stays on screen once it ended.
std::string MainWindow::elapsed_suffix() const {
    // Fl::now() hands back a timestamp, and seconds_since() turns it into the
    // elapsed time - the plain subtraction does not compile.
    const int secs = std::max(0, (int)Fl::seconds_since(m_run_started));
    char b[32];
    snprintf(b, sizeof(b), " %02d:%02d", secs / 60, secs % 60);
    return b;
}

void MainWindow::cb_run_tick(void *data) {
    auto *self = static_cast<MainWindow *>(data);
    if (!self->m_running) return;      // the run ended; let the clock stop

    self->m_progress->copy_label((std::string(tr(Str::Running)) +
                                  self->elapsed_suffix()).c_str());
    Fl::repeat_timeout(0.5, cb_run_tick, data);
}

void MainWindow::on_process_finished(int exit_code, const std::string &error) {
    set_running(false);

    // A run the user stopped is not a failure. No alert, no "failed" in the
    // progress line - the picture it may already have written still shows up.
    if (m_stop_requested.exchange(false)) {
        log(tr(Str::LogStopped));
        m_progress->value(0);
        m_progress->copy_label((std::string(tr(Str::Stopped)) + elapsed_suffix()).c_str());
        refresh_gallery();
        return;
    }

    if (!error.empty()) {
        log(std::string("[ERROR] ") + error);
        m_progress->copy_label((std::string(tr(Str::Failed)) + elapsed_suffix()).c_str());
    } else if (exit_code == 0) {
        m_progress->value(1);
        m_progress->copy_label((std::string(tr(Str::Done)) + elapsed_suffix()).c_str());
        // The grid is the window's view of the output folder, so a finished run
        // has to show up in it without the user asking. Newest sorts first, so
        // selecting entry 0 is the picture that was just written.
        refresh_gallery();
        if (m_gallery->mode() == GalleryView::Mode::Grid) m_gallery->select(0);
    } else {
        log(std::string(tr(Str::LogFailed)) + std::to_string(exit_code));
        m_progress->copy_label((std::string(tr(Str::Failed)) + elapsed_suffix()).c_str());
        char b[256];
        snprintf(b, sizeof(b), tr(Str::ErrExitCode), exit_code);
        log(b);
    }
}

void MainWindow::start_generation() {
    if (m_running) return;
    m_stop_requested = false;   // a fresh run, not a stopped one

    // Each run starts from a clean console: what is on screen should belong to
    // the run that is starting, not to whatever ran before it.
    if (m_log_buf) m_log_buf->text("");
    if (m_log_style) m_log_style->text("");

    GenOptions opt = collect_options();
    std::string err;
    if (!validate(opt, err)) {
        log(std::string(tr(Str::LogInvalid)) + err);
        return;
    }

    // The generator will not create the target folder for us.
    const std::string out_dir = m_output_dir->value();
    if (!out_dir.empty() && !ensure_directory(out_dir)) {
        const std::string msg = std::string(tr(Str::ErrOutputDir)) + out_dir;
        log(std::string(tr(Str::LogInvalid)) + msg);
        return;
    }

    // Never let the generator overwrite an older picture: a name that is
    // already on disk gets _1, _2, ... appended first.
    opt.output_path = unique_output_path();
    if (opt.output_path != output_path())
        log(std::string(tr(Str::LogOutputRenamed)) + opt.output_path);

    const std::string &exe = m_exe_path;
    const std::string &workdir = m_app_dir;
    // The command line is the one thing worth echoing: it is how you can tell
    // whether the options the form collected are the ones you meant.
    log("$ " + build_command_string(exe, opt));

    set_running(true);
    m_progress->value(0.15);
    m_run_started = Fl::now();
    // Start the clock on screen right away, not half a second later.
    m_progress->copy_label((std::string(tr(Str::Running)) + elapsed_suffix()).c_str());
    Fl::add_timeout(0.5, cb_run_tick, this);

    auto argv = build_argv(exe, opt);

    m_worker = std::thread([this, argv, workdir]() {
        RunResult r = run_process(argv, workdir, on_line_cb, this);
        // hop back to the FLTK main thread before touching widgets
        struct Payload { MainWindow *self; int code; std::string error; };
        Payload *p = new Payload{this, r.exit_code, r.error};
        Fl::awake([](void *data) {
            Payload *pp = (Payload *)data;
            pp->self->on_process_finished(pp->code, pp->error);
            delete pp;
        }, p);
    });
    if (m_worker.joinable()) m_worker.detach();
}

void MainWindow::stop_generation() {
    if (!m_running) return;
    m_stop_requested = true;
    log(tr(Str::StopSent));

    // Say it right away: the process still has to go away, and until it does
    // the progress line should already read as "stopped", not "running".
    m_progress->value(0);
    m_progress->copy_label((std::string(tr(Str::Stopped)) + elapsed_suffix()).c_str());

    // The child is a direct child of this process; kill by scanning is not
    // available, so we rely on pkill of the executable name.
    const std::string &exe = m_exe_path;
    size_t slash = exe.find_last_of('/');
    std::string base = (slash == std::string::npos) ? exe : exe.substr(slash + 1);
    if (!base.empty()) {
        std::string cmd = "pkill -TERM -f " + base;
        if (system(cmd.c_str()) != 0) log(tr(Str::StopNoProcess));
    }
}

// ----------------------------------------------------------------- presets ---

// Scale a length authored for the 20 px design font to the live font size.
int MainWindow::sc(int design) const { return design * g_m.font_base / 20; }

void MainWindow::build_preset_row(PresetRow &row, const char *key) {
    row.key = key;
    row.owner = this;
    // The dropdown carries no label of its own: its placeholder entry ("Config")
    // already says what it is, and the label row it shares is tight on width.
    row.choice = new GuardedChoice(0, 0, 0, 0);
    row.save = new Fl_Button(0, 0, 0, 0, tr(Str::PresetSave));
    row.rename = new Fl_Button(0, 0, 0, 0, tr(Str::PresetRename));
    row.del = new Fl_Button(0, 0, 0, 0, tr(Str::PresetDelete));

    row.choice->callback(cb_preset_choice, &row);
    // Fl_Choice only fires a callback when the picked entry differs from the
    // current one, so re-picking the entry that is already shown does nothing.
    // That is exactly the gesture that has to work here: after the selected
    // preset is deleted the dropdown is put back on "Default" while the form
    // still holds the deleted preset's values, and picking "Default" is how
    // they are restored. The same applies to re-picking the preset you are on
    // to discard hand edits. FL_WHEN_NOT_CHANGED lifts the "value changed"
    // condition, so every pick fires.
    row.choice->when(FL_WHEN_RELEASE | FL_WHEN_NOT_CHANGED);
    // Item callbacks (installed by rebuild_choice) are what actually apply a
    // preset: every pick fires them, while the widget callback above stays a
    // change-only safety net for a menu that somehow has no item callback.
    row.on_pick = [this, &row](int index) { apply_preset(row, index); };
    row.save->callback(cb_preset_save, &row);
    row.rename->callback(cb_preset_rename, &row);
    row.del->callback(cb_preset_delete, &row);
}

void MainWindow::style_preset_row(const PresetRow &row) {
    if (!row.choice) return;
    row.choice->labelsize(g_m.font_base);
    row.choice->textsize(g_m.font_base);
    row.choice->color(kFieldBg);
    row.choice->clear_visible_focus();   // no dotted ring around the picker
    for (Fl_Button *b : {row.save, row.rename, row.del}) {
        b->labelsize(g_m.font_base);
        b->color(kButtonBg);
    }
}

void MainWindow::layout_preset_row(const PresetRow &row, int x, int y, int w, int h) {
    if (!row.choice) return;
    // Right-aligned: the dropdown eats whatever the buttons leave, so the row
    // adapts to the column width and to the font scale. The buttons themselves
    // are measured from their captions: the design constants below were sized
    // for the Chinese wording, and the wider English "Rename" / "Delete" ran
    // past the button edge with the text clipped.
    const int gap = sc(8);
    const int pad = 2 * (Fl::box_dx(row.save->box()) + sc(8));

    auto fit = [&](Fl_Button *b) {
        int lw = 0, lh = 0;
        b->measure_label(lw, lh);        // in the button's own font
        return std::max(lw + pad, sc(48));
    };

    const int del_w = fit(row.del);
    const int ren_w = fit(row.rename);
    const int sav_w = fit(row.save);

    int bx = x + w;
    bx -= del_w;          row.del->resize(bx, y, del_w, h);
    bx -= gap + ren_w;    row.rename->resize(bx, y, ren_w, h);
    bx -= gap + sav_w;    row.save->resize(bx, y, sav_w, h);
    bx -= gap;

    int dw = bx - x;
    if (dw < sc(90)) dw = sc(90);
    row.choice->resize(x, y, dw, h);
}

void MainWindow::load_preset_rows() {
    if (!m_settings) return;
    Settings &cfg = *m_settings;
    m_prompt_presets.items =
        load_presets(cfg, m_prompt_presets.key, kPromptFields);
    m_negative_presets.items =
        load_presets(cfg, m_negative_presets.key, kPromptFields);
    m_param_presets.items =
        load_presets(cfg, m_param_presets.key, kParamFields);
    for (PresetRow *row : {&m_prompt_presets, &m_negative_presets, &m_param_presets}) {
        row->rebuild_choice();
        // Bring back the entry that was picked when the window was closed, so
        // the dropdowns still show which preset is in force.
        const int sel = cfg.get_int("sel_" + row->key, 0);
        row->choice->value(std::max(0, std::min(sel, (int)row->items.size())));
    }

    // Self-check: with QWEN_DUMP_LAYOUT=1 the loaded presets are printed, which
    // makes a load/apply mismatch visible without clicking anything.
    if (getenv("QWEN_DUMP_LAYOUT")) {
        for (const PresetRow *row : {&m_prompt_presets, &m_negative_presets, &m_param_presets}) {
            fprintf(stderr, "presets[%s]: %d loaded\n",
                    row->key.c_str(), (int)row->items.size());
            for (const Preset &p : row->items) {
                fprintf(stderr, "    \"%s\":", p.name.c_str());
                for (const auto &kv : p.fields)
                    fprintf(stderr, " %s=%s;", kv.first.c_str(), kv.second.c_str());
                fprintf(stderr, "\n");
            }
        }
    }
}

void MainWindow::store_preset_rows() {
    if (!m_settings) return;
    Settings &cfg = *m_settings;
    for (PresetRow *row : {&m_prompt_presets, &m_negative_presets, &m_param_presets}) {
        save_presets(cfg, row->key, row->items);
        // Which entry the dropdown is showing. Kept under its own name:
        // save_presets() wipes everything starting with "<key>_".
        cfg.set_int("sel_" + row->key, row->choice ? row->choice->value() : 0);
    }
}

Preset MainWindow::capture_preset(const PresetRow &row) const {
    Preset p;
    if (row.key == "prompt_preset") {
        p.fields.emplace_back("text", buffer_text(m_prompt_buf));
        return p;
    }
    if (row.key == "negative_preset") {
        p.fields.emplace_back("text", buffer_text(m_negative_buf));
        return p;
    }
    // The generation parameters, the reference images and the Z-Image inputs.
    // Only the output location stays out: that changes per run, not per style.
    // And only values that make sense go in: a preset must never carry a broken
    // number, so a stray keystroke cannot be saved and re-applied later.
    if (number_ok(m_cfg->value(), 0))
        p.fields.emplace_back("cfg", m_cfg->value());
    if (number_ok(m_control_scale->value(), 0))
        p.fields.emplace_back("ctrl_scale", m_control_scale->value());
    if (int_ok(m_width->value(), 1))
        p.fields.emplace_back("width", m_width->value());
    if (int_ok(m_height->value(), 1))
        p.fields.emplace_back("height", m_height->value());
    {
        // -l means different things to the two engines - 40 steps against auto
        // - so each keeps its own field in the preset. Sharing one field is
        // what made a preset saved under Qwen carry "40" into Z-Image, where
        // the empty (auto) value is the norm, and the other way round.
        const char *s = m_steps->value();
        const bool zimage = (m_backend == Backend::ZImage);
        // Empty is a real value for Z-Image (auto); for Qwen it is simply not
        // written, since the generator has no such default there.
        if (s && (!*s ? zimage : int_ok(s, 1)))
            p.fields.emplace_back(zimage ? "z_steps" : "steps", s);
    }
    if (int_ok(m_seed->value(), LLONG_MIN))
        p.fields.emplace_back("seed", m_seed->value());
    if (int_ok(m_batch->value(), 1))
        p.fields.emplace_back("batch", m_batch->value());
    p.fields.emplace_back("random_seed", m_random_seed->value() ? "1" : "0");
    const char *gpu = m_gpu->text();
    p.fields.emplace_back("gpu", gpu ? gpu : "auto");
    p.fields.emplace_back("mask", m_mask->value());
    p.fields.emplace_back("control", m_control->value());
    if (outpaint_ok(m_outpaint->value()))
        p.fields.emplace_back("outpaint", m_outpaint->value());
    if (m_zinput->value()[0]) p.fields.emplace_back("zinput", m_zinput->value());

    // The working mode is not part of this: it is a top-level choice of its own
    // (like the engine and the language), and applying a preset - or picking
    // "Default" - must not silently switch the form to a different mode.

    // The reference / input images belong to a preset too: a style often means
    // a particular set of pictures. One path per line, so paths with spaces
    // survive the round trip through the config file.
    std::string list;
    for (int i = 1; i <= m_inputs->size(); ++i) {
        if (!list.empty()) list += "\n";
        list += m_inputs->text(i);
    }
    if (!list.empty()) p.fields.emplace_back("inputs", list);
    return p;
}

void MainWindow::apply_default_values(PresetRow &row) {
    // The built-in Default entry. Everything here is compiled into the program
    // (see kDef* in the anonymous namespace).
    if (row.key == "prompt_preset")     { m_prompt_buf->text(""); return; }
    if (row.key == "negative_preset")   { m_negative_buf->text(""); return; }
    const Params &d = default_params(m_backend);
    m_cfg->value(d.cfg.c_str());
    m_width->value(d.width.c_str());
    m_height->value(d.height.c_str());
    m_steps->value(d.steps.c_str());
    m_seed->value(d.seed.c_str());
    m_batch->value(d.batch.c_str());
    m_random_seed->value(d.random_seed ? 1 : 0);
    for (int i = 0; i < m_gpu->size(); ++i) {
        const char *t = m_gpu->text(i);
        if (t && d.gpu == t) { m_gpu->value(i); break; }
    }
    m_control_scale->value(kDefCfg);
    m_mask->value("");
    m_control->value("");
    m_outpaint->value(kDefOutpaint);
    m_zinput->value("");
    m_inputs->clear();
}

void MainWindow::apply_preset(PresetRow &row, int index) {
    if (index >= (int)row.items.size()) return;

    // Index -1 is the built-in Default entry, not a user preset.
    if (index < 0) {
        apply_default_values(row);
        update_zimage_rows();
        layout_widgets();
        return;
    }

    const Preset &p = row.items[index];

    if (row.key == "prompt_preset" || row.key == "negative_preset") {
        Fl_Text_Buffer *buf =
            (row.key == "prompt_preset") ? m_prompt_buf : m_negative_buf;
        buf->text(p.value("text").c_str());
    } else {
        for (const auto &kv : p.fields) {
            const char *v = kv.second.c_str();
            if (kv.first == "cfg")              m_cfg->value(v);
            else if (kv.first == "ctrl_scale")  m_control_scale->value(v);
            else if (kv.first == "width")       m_width->value(v);
            else if (kv.first == "height")      m_height->value(v);
            else if (kv.first == "steps") {
                // Qwen's -l. Ignored while the Z-Image engine is active: its
                // own field (z_steps) is the one that applies there.
                if (m_backend != Backend::ZImage) m_steps->value(v);
            }
            else if (kv.first == "z_steps") {
                if (m_backend == Backend::ZImage) m_steps->value(v);
            }
            else if (kv.first == "seed")        m_seed->value(v);
            else if (kv.first == "batch")       m_batch->value(v);
            else if (kv.first == "random_seed") m_random_seed->value(kv.second == "1");
            else if (kv.first == "mask")        m_mask->value(v);
            else if (kv.first == "control")     m_control->value(v);
            else if (kv.first == "outpaint")    m_outpaint->value(v);
            else if (kv.first == "zinput")      m_zinput->value(v);
            else if (kv.first == "inputs") {
                m_inputs->clear();
                std::string line;
                for (char c : kv.second) {
                    if (c == '\n') {
                        if (!line.empty()) m_inputs->add(line.c_str());
                        line.clear();
                    } else {
                        line += c;
                    }
                }
                if (!line.empty()) m_inputs->add(line.c_str());
            }
            else if (kv.first == "gpu") {
                // Match by text, not index: the dropdown order is fixed but
                // storing a label keeps the file readable.
                for (int i = 0; i < m_gpu->size(); ++i) {
                    const char *t = m_gpu->text(i);
                    if (t && kv.second == t) { m_gpu->value(i); break; }
                }
            }
        }
    }
}

void MainWindow::save_current_as_preset(PresetRow &row) {
    Preset captured = capture_preset(row);

    // Saving usually means "update the one I am on", so the name box starts
    // with the selected preset's name rather than being empty every time.
    const int sel = row.picked();
    std::string name = (sel >= 0) ? row.items[sel].name : "";

    if (!ask_preset_name(tr(Str::PresetSaveTitle), tr(Str::PresetNameLabel),
                         tr(Str::PickerOk), name))
        return;

    // Keeping that same name means replacing the entry, and that is worth a
    // confirmation: Save sits next to Delete.
    if (sel >= 0 && name == row.items[sel].name) {
        char q[512];
        std::snprintf(q, sizeof(q), tr(Str::PresetOverwriteAsk), name.c_str());
        if (!ask_confirm(tr(Str::PresetSaveTitle), q, tr(Str::PresetOverwrite),
                         tr(Str::PickerCancel)))
            return;
        overwrite_preset(row, sel, captured);
        log(std::string(tr(Str::PresetSaved)) + name);
        return;
    }

    // A different name: it has to be free, or the dropdown would hold two
    // entries that look the same.
    for (const Preset &existing : row.items) {
        if (existing.name == name) {
            log(tr(Str::PresetNameTaken));
            return;
        }
    }

    captured.name = name;
    row.items.push_back(captured);
    row.rebuild_choice();
    row.choice->value((int)row.items.size());   // keep the saved one showing
    save_settings();   // write it through now: a crash must not lose it
    log(std::string(tr(Str::PresetSaved)) + name);
}

void MainWindow::overwrite_preset(PresetRow &row, int index, const Preset &values) {
    if (index < 0 || index >= (int)row.items.size()) return;
    Preset &target = row.items[index];

    // Merge, never replace. A capture only holds the fields that belong to the
    // engine that is active plus the shared ones; the other engine's fields
    // (its own -l, its ControlNet scale, its image slots) are not in there, and
    // wiping them is what used to leave the other engine with an empty -l
    // after switching over.
    //
    // A field the capture does produce replaces the old value - and if it was
    // not in the entry at all, this is where it gets added, which is how a
    // preset first saved under one engine picks up the other engine's -l once
    // you save it there too.
    for (const auto &kv : values.fields) {
        auto it = std::remove_if(target.fields.begin(), target.fields.end(),
                                 [&kv](const std::pair<std::string, std::string> &e) {
                                     return e.first == kv.first;
                                 });
        target.fields.erase(it, target.fields.end());
        target.fields.push_back(kv);
    }

    row.rebuild_choice();
    row.choice->value(index + 1);
    save_settings();
}

void MainWindow::rename_selected_preset(PresetRow &row) {
    const int sel = row.picked();
    if (sel < 0) { log(tr(Str::PresetNeedSelection)); return; }

    std::string name = row.items[sel].name;
    if (!ask_preset_name(tr(Str::PresetRenameTitle), tr(Str::PresetNameLabel),
                         tr(Str::PickerOk), name))
        return;

    for (size_t i = 0; i < row.items.size(); ++i) {
        if ((int)i != sel && row.items[i].name == name) {
            log(tr(Str::PresetNameTaken));
            return;
        }
    }

    row.items[sel].name = name;
    row.rebuild_choice();
    row.choice->value(sel + 1);
    save_settings();
}

void MainWindow::delete_selected_preset(PresetRow &row) {
    const int sel = row.picked();
    if (sel < 0) { log(tr(Str::PresetNeedSelection)); return; }

    const std::string name = row.items[sel].name;
    char q[512];
    std::snprintf(q, sizeof(q), tr(Str::PresetDeleteAsk), name.c_str());
    if (!ask_confirm(tr(Str::PresetDelete), q, tr(Str::PresetDelete),
                     tr(Str::PickerCancel)))
        return;

    row.items.erase(row.items.begin() + sel);
    row.rebuild_choice();
    save_settings();
}

void MainWindow::cb_preset_choice(Fl_Widget *, void *data) {
    auto *row = static_cast<PresetRow *>(data);
    static_cast<MainWindow *>(row->owner)->apply_preset(*row, row->picked());
    Fl::focus(nullptr);   // drop the dotted focus ring
}

void MainWindow::cb_preset_save(Fl_Widget *, void *data) {
    auto *row = static_cast<PresetRow *>(data);
    static_cast<MainWindow *>(row->owner)->save_current_as_preset(*row);
}

void MainWindow::cb_preset_rename(Fl_Widget *, void *data) {
    auto *row = static_cast<PresetRow *>(data);
    static_cast<MainWindow *>(row->owner)->rename_selected_preset(*row);
}

void MainWindow::cb_preset_delete(Fl_Widget *, void *data) {
    auto *row = static_cast<PresetRow *>(data);
    static_cast<MainWindow *>(row->owner)->delete_selected_preset(*row);
}

// ------------------------------------------------------------- callbacks ---

void MainWindow::open_output_dir() {
    std::string dir = m_output_dir->value();
    if (dir.empty()) dir = m_app_dir;

    if (!ensure_directory(dir)) {
        const std::string msg = std::string(tr(Str::ErrOutputDir)) + dir;
        log(std::string(tr(Str::LogInvalid)) + msg);
        return;
    }

    // xdg-open hands the folder to whatever file manager the desktop uses, but
    // it does not raise the resulting window: the folder lands behind every
    // other window, which reads as "the button did nothing". The freedesktop
    // FileManager1 interface is specified to *show* the folder, and desktops
    // honour that by activating the file manager window. It takes two arguments
    // (uriList, startUpId); omitting startUpId makes the call fail with
    // UnknownMethod because the signature does not match.
    //
    // The whole chain runs in the background: a hung file manager must not
    // freeze the UI, and xdg-open is still the fallback for desktops without
    // the FileManager1 service.
    const std::string cmd =
        "( dbus-send --session --print-reply "
        "--dest=org.freedesktop.FileManager1 --type=method_call "
        "/org/freedesktop/FileManager1 "
        "org.freedesktop.FileManager1.ShowFolders array:string:\"" +
        path_to_file_uri(dir) + "\" string:\"\" >/dev/null 2>&1 "
        "|| xdg-open \"" + dir + "\" >/dev/null 2>&1 ) &";
    if (system(cmd.c_str()) != 0)
        log(std::string("[error] cannot open ") + dir);
}

void MainWindow::on_line_cb(const std::string &line, void *user) {
    // Called on the worker thread. Copy the string and post to the main thread.
    MainWindow *self = (MainWindow *)user;
    std::string *copy = new std::string(line);
    Fl::awake([](void *d) {
        auto *pair = (std::pair<MainWindow *, std::string *> *)d;
        pair->first->append_log(*pair->second);
        delete pair->second;
        delete pair;
    }, new std::pair<MainWindow *, std::string *>(self, copy));
}

void MainWindow::cb_generate(Fl_Widget *, void *d) { ((MainWindow *)d)->start_generation(); }
void MainWindow::cb_stop(Fl_Widget *, void *d) { ((MainWindow *)d)->stop_generation(); }
void MainWindow::cb_latest(Fl_Widget *, void *d) { ((MainWindow *)d)->m_gallery->show_latest(); }
void MainWindow::cb_refresh(Fl_Widget *, void *d) { ((MainWindow *)d)->refresh_gallery(); }
void MainWindow::cb_gallery_back(Fl_Widget *, void *d) {
    ((MainWindow *)d)->m_gallery->set_mode(GalleryView::Mode::Grid);
}
void MainWindow::cb_add_input(Fl_Widget *, void *d) { ((MainWindow *)d)->pick_input_images(); }
void MainWindow::cb_del_input(Fl_Widget *, void *d) { ((MainWindow *)d)->remove_selected_input(); }
void MainWindow::cb_browse_mask(Fl_Widget *, void *d) {
    auto *self = (MainWindow *)d;
    self->pick_image_into(self->m_mask, Str::MaskImage);
}
void MainWindow::cb_browse_zinput(Fl_Widget *, void *d) {
    auto *self = (MainWindow *)d;
    self->pick_image_into(self->m_zinput, Str::SelectInputImage);
    self->update_outpaint_size();   // a new picture means a new canvas size
}

void MainWindow::cb_zinput_changed(Fl_Widget *, void *d) {
    ((MainWindow *)d)->update_outpaint_size();
}

void MainWindow::cb_outpaint_changed(Fl_Widget *, void *d) {
    ((MainWindow *)d)->update_outpaint_size();
}
void MainWindow::cb_zmode(Fl_Widget *, void *d) {
    auto *self = (MainWindow *)d;
    // The rows the new mode needs appear, the others go away, and the column is
    // laid out again for the shorter or taller block. The image slots also say
    // different things per mode ("pick a control image" against "pick the image
    // to upscale"), so their hints are refreshed too.
    self->update_zimage_rows();
    self->update_param_hints();
    self->layout_widgets();
    self->save_settings();
    self->redraw();
    Fl::focus(nullptr);   // drop the dotted focus ring
}

void MainWindow::cb_zmodel(Fl_Widget *, void *d) {
    auto *self = (MainWindow *)d;
    // The model decides which modes exist at all, which in turn decides which
    // rows the form shows and where -m points.
    self->update_model_path();
    self->update_zmodel_modes();

    // Leaving a mode the new model cannot run: fall back to plain text-to-image.
    if (self->m_zmodel->value() == (int)ZModel::Base) {
        const ZMode mode = (ZMode)self->m_zmode->value();
        if (mode == ZMode::ControlNet || mode == ZMode::TileUpscale) {
            self->m_zmode->value((int)ZMode::TextToImage);
            self->log(tr(Str::LogZModelNoControl));
        }
    }

    self->update_zimage_rows();
    self->layout_widgets();
    self->save_settings();
    self->redraw();
    Fl::focus(nullptr);
}
void MainWindow::cb_browse_control(Fl_Widget *, void *d) {
    auto *self = (MainWindow *)d;
    self->pick_image_into(self->m_control, Str::ControlImage);
}
void MainWindow::cb_browse_output(Fl_Widget *, void *d) { ((MainWindow *)d)->pick_output_dir(); }
void MainWindow::cb_open_output_dir(Fl_Widget *, void *d) { ((MainWindow *)d)->open_output_dir(); }
void MainWindow::cb_about(Fl_Widget *, void *) {
    // The project page; fl_open_uri hands the URL to the desktop's browser.
    fl_open_uri(kRepoUrl);
}
void MainWindow::cb_zoom_fit(Fl_Widget *, void *d) { ((MainWindow *)d)->m_gallery->zoom_fit(); }
void MainWindow::cb_zoom_1to1(Fl_Widget *, void *d) { ((MainWindow *)d)->m_gallery->zoom_1to1(); }

void MainWindow::cb_backend(Fl_Widget *w, void *d) {
    auto *self = (MainWindow *)d;
    auto *ch = (Fl_Choice *)w;
    self->set_backend(ch->value() == 1 ? Backend::ZImage : Backend::Qwen);
    Fl::focus(nullptr);   // drop the dotted focus ring
}

void MainWindow::cb_language(Fl_Widget *w, void *d) {
    auto *self = (MainWindow *)d;
    auto *ch = (Fl_Choice *)w;
    Lang lang = (ch->value() == 1) ? Lang::ChineseSimplified : Lang::English;
    if (lang != i18n_language())
        self->apply_language(lang);
    Fl::focus(nullptr);   // drop the dotted focus ring
}
