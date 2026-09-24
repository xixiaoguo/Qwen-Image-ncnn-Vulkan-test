#include "ui_main.h"
#include "picker.h"
#include "settings.h"

#include <FL/Fl.H>
#include <FL/fl_ask.H>

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

bool file_exists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) != 0;
}

bool is_image_name(const std::string &name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot + 1);
    for (char &c : ext) c = (char)tolower((unsigned char)c);
    return ext == "png" || ext == "jpg" || ext == "jpeg" ||
           ext == "webp" || ext == "bmp" || ext == "gif";
}

// Most recently modified image file inside dir, or "" when there is none.
std::string newest_image_in(const std::string &dir) {
    DIR *d = opendir(dir.c_str());
    if (!d) return "";
    std::string best;
    time_t best_mtime = 0;
    while (struct dirent *e = readdir(d)) {
        const std::string name = e->d_name;
        if (name.empty() || name[0] == '.' || !is_image_name(name)) continue;
        const std::string path = dir + "/" + name;
        struct stat st;
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (best.empty() || st.st_mtime >= best_mtime) {
            best = path;
            best_mtime = st.st_mtime;
        }
    }
    closedir(d);
    return best;
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
    "cfg", "width", "height", "steps", "seed", "batch", "random_seed", "gpu"};

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
constexpr int         kDefGpu    = 0;      // index of "auto" in the GPU dropdown

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
    m_app_dir    = resolve_app_dir();
    m_exe_path   = m_app_dir + "/qwenimage-ncnn-vulkan";

    // Accept both layouts that show up in practice: models/qwenimage21 (the
    // upstream convention) and a bare qwenimage21 next to the binary.
    {
        const std::string with_models = m_app_dir + "/models/qwenimage21";
        const std::string bare        = m_app_dir + "/qwenimage21";
        struct stat st;
        if (stat(with_models.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            m_model_path = with_models;
        else if (stat(bare.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            m_model_path = bare;
        else
            m_model_path = with_models;   // report the conventional one
    }
    m_settings   = std::make_unique<Settings>(m_app_dir + "/qwenimage-gui.conf");
    m_settings->load();

    // Derive every UI metric from the font size *before* the first widget is
    // created: the sizes feed both styling and layout. The size is detected
    // from the display; ui_font_size in the config file only overrides it.
    const int cfg_font = m_settings->get_int("ui_font_size", 0);
    metrics_from_font(cfg_font > 0 ? cfg_font : auto_font_size());

    build_widgets();
    style_widgets();
    layout_widgets();
    end();

    // Minimum size follows the metrics instead of hard-coded pixels.
    size_range(g_m.left_w + 520, g_m.row_h * 20);

    load_settings();

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

    log(std::string(tr(Str::InitBinary)) + m_exe_path);
    log(std::string(tr(Str::InitModel)) + m_model_path);
    log(std::string(tr(Str::InitConfig)) + m_settings->path());
    log("[init] ui base font: " + std::to_string(g_m.font_base) +
        (cfg_font > 0 ? " (from ui_font_size)" : " (auto-detected)"));
    log(tr(Str::ReadyHint));
    log(tr(Str::OptionsHint));
}

MainWindow::~MainWindow() {
    if (m_running) {
        if (m_child_pid > 0) kill(m_child_pid, SIGTERM);
        if (m_worker.joinable()) m_worker.join();
    }
    save_settings();   // safe: the text buffers are still alive here
    delete m_prompt_buf;
    delete m_negative_buf;
    delete m_log_buf;
    m_log_buf = nullptr;
}

void MainWindow::resize(int X, int Y, int W, int H) {
    Fl_Double_Window::resize(X, Y, W, H);
    if (m_viewer) layout_widgets();   // ignored until the widgets exist
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

// --------------------------------------------------------------- widgets ---

void MainWindow::build_widgets() {
    // --- language selector (top row) ---
    m_lbl_lang = new Fl_Box(FL_NO_BOX, 0, 0, 0, 0, tr(Str::Language));
    m_lbl_lang->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    m_lang_choice = new Fl_Choice(0, 0, 0, 0);
    m_lang_choice->add(language_name(Lang::English));            // index 0
    m_lang_choice->add(language_name(Lang::ChineseSimplified));  // index 1
    m_lang_choice->value(i18n_language() == Lang::ChineseSimplified ? 1 : 0);
    m_lang_choice->callback(cb_language, this);

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
    m_prompt = new Fl_Text_Editor(0, 0, 0, 0);
    m_prompt->buffer(m_prompt_buf);
    m_prompt->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
    m_prompt->linenumber_width(0);

    // --- negative prompt ---
    m_lbl_negative = new Fl_Box(FL_NO_BOX, 0, 0, 0, 0, tr(Str::NegativeOptional));
    m_lbl_negative->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_negative_buf = new Fl_Text_Buffer();
    m_negative = new Fl_Text_Editor(0, 0, 0, 0);
    m_negative->buffer(m_negative_buf);
    m_negative->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
    m_negative->linenumber_width(0);

    // --- CFG ---
    m_cfg = new Fl_Float_Input(0, 0, 0, 0, tr(Str::Cfg));
    m_lbl_cfg = m_cfg;
    m_cfg->value(kDefCfg);

    // --- output: folder + file name + format ---
    m_output_dir = new Fl_Input(0, 0, 0, 0, tr(Str::OutputDir));
    m_lbl_output_dir = m_output_dir;
    m_output_dir->value(".");

    m_btn_browse_output = new Fl_Button(0, 0, 0, 0, tr(Str::Browse));
    m_btn_browse_output->callback(cb_browse_output, this);

    m_output_name = new Fl_Input(0, 0, 0, 0, tr(Str::OutputName));
    m_lbl_output_name = m_output_name;
    m_output_name->value("out");

    m_output_format = new Fl_Choice(0, 0, 0, 0, tr(Str::OutputFormat));
    m_lbl_output_format = m_output_format;
    // Exactly the formats the generator documents, and no more: a png/jpg/webp
    // suffix is what it switches on. Any other extension (bmp, tif, ...) is
    // quietly written as PNG data under that misleading name.
    m_output_format->add("png");
    m_output_format->add("jpg");
    m_output_format->add("webp");
    m_output_format->value(0);

    // --- size ---
    m_width = new Fl_Int_Input(0, 0, 0, 0, tr(Str::Size));
    m_lbl_size = m_width;
    m_width->value(kDefWidth);

    m_height = new Fl_Int_Input(0, 0, 0, 0);
    m_height->value(kDefHeight);

    // Sizes are all multiples of 32 so they satisfy both text-to-image (16) and
    // image-editing (32) validation. "720p"/"1080p" entries sit on the nearest
    // conforming size: 720p -> 736 lines, 1080p -> 1088 lines.
    m_preset_menu = new Fl_Menu_Button(0, 0, 0, 0, tr(Str::Preset));
    m_preset_menu->add("512 x 512");
    m_preset_menu->add("768 x 768");
    m_preset_menu->add("1024 x 1024");
    m_preset_menu->add("1024 x 768");
    m_preset_menu->add("1280 x 720   (720p)");
    m_preset_menu->add("1920 x 1080  (1080p)");
    m_preset_menu->add("2560 x 1440  (1440p)");
    m_preset_menu->add("2048 x 2048");
    m_preset_menu->callback([](Fl_Widget *w, void *d) {
        auto *self = static_cast<MainWindow *>(d);
        auto *menu = static_cast<Fl_Menu_Button *>(w);
        const Fl_Menu_Item *item = menu->mvalue();
        if (!item || !item->label()) return;
        int ww = 0, hh = 0;
        if (sscanf(item->label(), "%d x %d", &ww, &hh) == 2) {
            self->m_width->value(std::to_string(ww).c_str());
            self->m_height->value(std::to_string(hh).c_str());
        }
    }, this);

    // --- steps / seed ---
    m_steps = new Fl_Int_Input(0, 0, 0, 0, tr(Str::Steps));
    m_lbl_steps = m_steps;
    m_steps->value(kDefSteps);

    m_seed = new Fl_Int_Input(0, 0, 0, 0, tr(Str::Seed));
    m_lbl_seed = m_seed;
    m_seed->value(kDefSeed);

    // --- batch / random seed ---
    m_batch = new Fl_Int_Input(0, 0, 0, 0, tr(Str::Batch));
    m_lbl_batch = m_batch;
    m_batch->value(kDefBatch);

    m_random_seed = new Fl_Check_Button(0, 0, 0, 0, tr(Str::RandomSeed));

    // --- GPU ---
    m_gpu = new Fl_Choice(0, 0, 0, 0, tr(Str::Gpu));
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

    m_inputs = new Fl_Hold_Browser(0, 0, 0, 0);

    m_btn_add_image = new Fl_Button(0, 0, 0, 0, tr(Str::AddImage));
    m_btn_add_image->callback(cb_add_input, this);
    m_btn_del_image = new Fl_Button(0, 0, 0, 0, tr(Str::RemoveSelected));
    m_btn_del_image->callback(cb_del_input, this);

    // --- preview toolbar ---
    m_btn_zoom_in = new Fl_Button(0, 0, 0, 0, tr(Str::ZoomIn));
    m_btn_zoom_in->callback(cb_zoom_in, this);
    m_btn_zoom_out = new Fl_Button(0, 0, 0, 0, tr(Str::ZoomOut));
    m_btn_zoom_out->callback(cb_zoom_out, this);
    m_btn_fit = new Fl_Button(0, 0, 0, 0, tr(Str::Fit));
    m_btn_fit->callback(cb_zoom_fit, this);
    m_btn_1to1 = new Fl_Button(0, 0, 0, 0, tr(Str::Zoom1to1));
    m_btn_1to1->callback(cb_zoom_1to1, this);
    m_btn_preview = new Fl_Button(0, 0, 0, 0, tr(Str::PreviewLatest));
    m_btn_preview->callback(cb_preview, this);
    m_btn_reload = new Fl_Button(0, 0, 0, 0, tr(Str::Reload));
    m_btn_reload->callback(cb_preview, this);

    m_btn_open_dir = new Fl_Button(0, 0, 0, 0, tr(Str::OpenOutputDir));
    m_btn_open_dir->callback(cb_open_output_dir, this);

    // --- preview + console ---
    m_viewer = new ImageViewer(0, 0, 0, 0);

    m_log_view = new Fl_Text_Display(0, 0, 0, 0);
    m_log_buf = new Fl_Text_Buffer();
    m_log_view->buffer(m_log_buf);

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
                           m_width, m_height, m_steps, m_seed, m_batch};
    for (Fl_Input_ *w : fields) {
        w->labelsize(g_m.font_base);
        w->textsize(g_m.font_base);
        w->color(kFieldBg);
        w->selection_color(kAccent);
    }

    // --- choices ---
    for (Fl_Choice *c : {m_gpu, m_lang_choice, m_output_format}) {
        c->labelsize(g_m.font_base);
        c->textsize(g_m.font_base);
        c->color(kFieldBg);
    }

    m_preset_menu->labelsize(g_m.font_base);
    m_preset_menu->textsize(g_m.font_base);
    m_preset_menu->color(kButtonBg);

    // --- plain labels ---
    for (Fl_Widget *w : {m_lbl_lang, m_lbl_prompt, m_lbl_negative})
        w->labelsize(g_m.font_base);

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
                         m_btn_zoom_in, m_btn_zoom_out, m_btn_fit, m_btn_1to1,
                         m_btn_preview, m_btn_reload, m_btn_open_dir};
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
    // The text areas grow to absorb the space the tightened rows give back.
    const int prompt_h   = std::max(S(40), std::min(S(220), (int)(H * 0.14)));
    const int negative_h = std::max(S(32), std::min(S(160), (int)(H * 0.10)));

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

    // language row
    m_lbl_lang->resize(lx, ly + S(9), g_m.label_w - S(20), S(24));
    m_lang_choice->resize(lx + g_m.label_w, ly, S(240), g_m.row_h);
    ly += g_m.row_h + S(18);

    m_sep_top->resize(lx, ly, lw, 2);
    ly += S(18);

    // The header covers the whole parameter block, so it keeps the full width.
    m_grp_gen->resize(lx, ly, lw, S(30));
    ly += S(36);

    // prompt
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

    // CFG
    m_cfg->resize(lx + g_m.label_w, ly, S(140), g_m.row_h);
    ly += g_m.row_h + g_m.gap;

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

    // reference images
    m_sep_ref->resize(lx, ly, lw, 2);
    ly += S(18);
    m_lbl_ref->resize(lx, ly, lw, S(30));
    ly += S(36);

    const int ref_h = std::max(S(56), std::min(S(320), H - ly - g_m.pad * 2));
    m_inputs->resize(lx, ly, lw - S(196), ref_h);
    m_btn_add_image->resize(lx + lw - S(180), ly, S(180), g_m.row_h);
    m_btn_del_image->resize(lx + lw - S(180), ly + S(46), S(180), g_m.row_h);

    // ============ right column: preview + console ============
    const int rx = g_m.pad + g_m.left_w + g_m.pad;
    const int rw = W - rx - g_m.pad;
    int ry = g_m.pad;

    // preview toolbar
    {
        int bx = rx;
        m_btn_zoom_out->resize(bx, ry, S(52), g_m.row_h); bx += S(56);
        m_btn_zoom_in->resize(bx, ry, S(52), g_m.row_h);  bx += S(56);
        m_btn_fit->resize(bx, ry, S(104), g_m.row_h);     bx += S(108);
        m_btn_1to1->resize(bx, ry, S(84), g_m.row_h);     bx += S(88);
        m_btn_preview->resize(bx, ry, S(160), g_m.row_h); bx += S(164);
        m_btn_reload->resize(bx, ry, S(136), g_m.row_h); bx += S(140);
        m_btn_open_dir->resize(bx, ry, S(150), g_m.row_h);
    }
    ry += g_m.row_h + g_m.gap;

    int body = H - ry - g_m.pad - g_m.action_h - g_m.gap;
    if (body < S(300)) body = S(300);

    int preview_h = std::max(S(200), body * 56 / 100);
    m_viewer->resize(rx, ry, rw, preview_h);
    ry += preview_h + g_m.gap;

    int log_h = H - ry - g_m.pad - g_m.action_h - g_m.gap;
    if (log_h < S(120)) log_h = S(120);
    m_log_view->resize(rx, ry, rw, log_h);
    ry += log_h + g_m.gap;

    // progress + actions
    const int gen_w = S(220), stop_w = S(190);
    int prog_w = rw - gen_w - stop_w - 2 * g_m.gap;
    if (prog_w < S(150)) prog_w = S(150);
    m_progress->resize(rx, ry, prog_w, g_m.action_h);
    m_btn_generate->resize(rx + rw - stop_w - gen_w - g_m.gap, ry, gen_w, g_m.action_h);
    m_btn_stop->resize(rx + rw - stop_w, ry, stop_w, g_m.action_h);
}

// --------------------------------------------------------------- i18n ----

std::string MainWindow::T(Str s) const { return std::string(tr(s)); }

void MainWindow::apply_language(Lang lang) {
    i18n_set_language(lang);

    // Window title is handled by the caller (main) and here for runtime switches.
    label(tr(Str::AppTitle));

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
    if (m_grp_gen)      m_grp_gen->copy_label(tr(Str::GroupGeneration));
    if (m_lbl_prompt)   m_lbl_prompt->copy_label(tr(Str::PromptRequired));
    if (m_lbl_negative) m_lbl_negative->copy_label(tr(Str::NegativeOptional));
    if (m_lbl_ref)      m_lbl_ref->copy_label(tr(Str::RefImages));

    // Buttons.
    if (m_preset_menu)        m_preset_menu->copy_label(tr(Str::Preset));
    if (m_btn_browse_output)  m_btn_browse_output->copy_label(tr(Str::Browse));
    if (m_btn_add_image)      m_btn_add_image->copy_label(tr(Str::AddImage));
    if (m_btn_del_image)      m_btn_del_image->copy_label(tr(Str::RemoveSelected));
    if (m_btn_preview)        m_btn_preview->copy_label(tr(Str::PreviewLatest));
    if (m_btn_reload)         m_btn_reload->copy_label(tr(Str::Reload));
    if (m_btn_open_dir)       m_btn_open_dir->copy_label(tr(Str::OpenOutputDir));
    if (m_btn_fit)            m_btn_fit->copy_label(tr(Str::Fit));
    if (m_btn_zoom_in)        m_btn_zoom_in->copy_label(tr(Str::ZoomIn));
    if (m_btn_zoom_out)       m_btn_zoom_out->copy_label(tr(Str::ZoomOut));
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
    redraw();
}

// ------------------------------------------------------------------ log ---

void MainWindow::log(const std::string &s) {
    if (!m_log_buf) return;
    m_log_buf->append((s + "\n").c_str());
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
    opt.prompt = buffer_text(m_prompt_buf);
    if (opt.prompt.empty()) { err = tr(Str::ErrPromptRequired); return false; }

    opt.negative_prompt = buffer_text(m_negative_buf);

    double cfg = 1.0;
    if (sscanf(m_cfg->value(), "%lf", &cfg) != 1 || cfg < 0) {
        err = tr(Str::ErrCfg); return false;
    }
    opt.cfg_scale = cfg;

    opt.output_path = output_path();
    if (opt.output_path.empty()) { err = tr(Str::ErrOutput); return false; }

    // The model location is fixed: it sits next to this executable.
    opt.model_path = m_model_path;

    int w = atoi(m_width->value());
    int h = atoi(m_height->value());
    if (w <= 0 || h <= 0) { err = tr(Str::ErrSizePositive); return false; }
    int mult = opt.inputs.empty() ? 16 : 32; // editing needs 32, t2i needs 16
    if (!opt.inputs.empty()) mult = 32;
    if (w % mult || h % mult) {
        char b[256];
        snprintf(b, sizeof(b), tr(Str::ErrSizeMultiple),
                 mult, opt.inputs.empty() ? tr(Str::TextToImage) : tr(Str::ImageEditing),
                 w, h);
        err = b; return false;
    }
    opt.width = w; opt.height = h;

    opt.steps = atoi(m_steps->value());
    if (opt.steps <= 0) { err = tr(Str::ErrSteps); return false; }

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

    if (opt.inputs.size() > 10) {
        err = tr(Str::ErrTooManyImages); return false;
    }
    return true;
}

GenOptions MainWindow::collect_options() {
    GenOptions o;
    o.prompt = buffer_text(m_prompt_buf);
    o.negative_prompt = buffer_text(m_negative_buf);
    sscanf(m_cfg->value(), "%lf", &o.cfg_scale);
    o.output_path = output_path();
    o.width = atoi(m_width->value());
    o.height = atoi(m_height->value());
    o.steps = atoi(m_steps->value());
    o.seed = atoll(m_seed->value());
    o.model_path = m_model_path;
    o.batch = atoi(m_batch->value());
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

    for (const std::string &f : files) {
        if (m_inputs->size() >= 10) { fl_alert("%s", tr(Str::MaxRefImages)); break; }
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
    if (!dir.empty()) m_output_dir->value(dir.c_str());
}

// ------------------------------------------------------------- settings ---

std::string MainWindow::output_path() const {
    std::string dir = m_output_dir->value();
    if (dir.empty()) dir = m_app_dir;
    while (dir.size() > 1 && dir.back() == '/') dir.pop_back();

    std::string name = m_output_name->value();
    if (name.empty()) name = "out";
    // The format box decides the extension, so drop anything the user typed.
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);

    const char *fmt = m_output_format->text();
    return dir + "/" + name + "." + ((fmt && *fmt) ? fmt : "png");
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
    m_cfg->value(s.get("cfg", "1.0").c_str());
    m_width->value(s.get("width", "1024").c_str());
    m_height->value(s.get("height", "1024").c_str());
    m_steps->value(s.get("steps", "40").c_str());
    m_seed->value(s.get("seed", "42").c_str());
    m_batch->value(s.get("batch", "1").c_str());
    m_random_seed->value(s.get_bool("random_seed", false) ? 1 : 0);

    const std::string gpu = s.get("gpu", "auto");
    int gi = 0;
    if (gpu == "cpu") gi = 1;
    else if (gpu.size() == 1 && gpu[0] >= '0' && gpu[0] <= '3') gi = 2 + (gpu[0] - '0');
    m_gpu->value(gi);

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

    load_preset_rows();
}

void MainWindow::save_settings() {
    if (!m_settings) return;
    Settings &s = *m_settings;

    s.set("language", i18n_language() == Lang::ChineseSimplified ? "zh" : "en");
    s.set("prompt", buffer_text(m_prompt_buf));
    s.set("negative", buffer_text(m_negative_buf));
    s.set("cfg", m_cfg->value());
    s.set("width", m_width->value());
    s.set("height", m_height->value());
    s.set("steps", m_steps->value());
    s.set("seed", m_seed->value());
    s.set("batch", m_batch->value());
    s.set_bool("random_seed", m_random_seed->value() != 0);

    const char *gpu = m_gpu->text();
    s.set("gpu", gpu ? gpu : "auto");

    s.set("output_dir", m_output_dir->value());
    s.set("output_name", m_output_name->value());
    const char *fmt = m_output_format->text();
    s.set("output_format", fmt ? fmt : "png");

    const int n = m_inputs->size();
    s.set_int("input_count", n);
    for (int i = 1; i <= n; ++i)
        s.set("input_" + std::to_string(i - 1), m_inputs->text(i));

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
        m_btn_generate->activate();
        m_btn_stop->deactivate();
    }
}

void MainWindow::on_process_finished(int exit_code, const std::string &error) {
    set_running(false);
    if (!error.empty()) {
        log(std::string("[ERROR] ") + error);
        m_progress->copy_label(tr(Str::Failed));
        fl_alert("%s", error.c_str());
    } else if (exit_code == 0) {
        log(tr(Str::LogDone));
        m_progress->value(1);
        m_progress->copy_label(tr(Str::Done));
        preview_latest_output();
    } else {
        log(std::string(tr(Str::LogFailed)) + std::to_string(exit_code));
        m_progress->copy_label(tr(Str::Failed));
        char b[256];
        snprintf(b, sizeof(b), tr(Str::ErrExitCode), exit_code);
        fl_alert("%s", b);
    }
}

void MainWindow::preview_latest_output() {
    const std::string out = output_path();
    if (out.empty()) return;

    std::string candidate = out;

    // A batch run writes out-0.png, out-1.png, ... Take the newest slice, which
    // is the last one the generator finished writing.
    const int batch = atoi(m_batch->value());
    if (batch > 1) {
        const size_t dot = out.find_last_of('.');
        const size_t slash = out.find_last_of('/');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
            const std::string stem = out.substr(0, dot);
            const std::string ext  = out.substr(dot);
            std::string newest;
            time_t newest_mtime = 0;
            for (int i = 0; i < batch; ++i) {
                const std::string p = stem + "-" + std::to_string(i) + ext;
                struct stat st;
                if (stat(p.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
                if (newest.empty() || st.st_mtime >= newest_mtime) {
                    newest = p;
                    newest_mtime = st.st_mtime;
                }
            }
            if (!newest.empty()) candidate = newest;
        }
    }

    // Nothing at the expected path: show whatever image appeared in the output
    // folder most recently, so a successful run is never left invisible.
    if (!file_exists(candidate)) {
        std::string dir = m_output_dir->value();
        if (dir.empty()) dir = m_app_dir;
        const std::string newest = newest_image_in(dir);
        if (newest.empty()) {
            log(std::string(tr(Str::LogPreviewNotFound)) + candidate);
            return;
        }
        candidate = newest;
    }

    if (!m_viewer->load(candidate))
        log(std::string(tr(Str::LogPreviewNotFound)) + m_viewer->error());
    else
        log(std::string(tr(Str::LogPreviewLoaded)) + candidate);
}

void MainWindow::start_generation() {
    if (m_running) return;

    GenOptions opt = collect_options();
    std::string err;
    if (!validate(opt, err)) {
        fl_alert("%s", err.c_str());
        log(std::string(tr(Str::LogInvalid)) + err);
        return;
    }

    // The generator will not create the target folder for us.
    const std::string out_dir = m_output_dir->value();
    if (!out_dir.empty() && !ensure_directory(out_dir)) {
        const std::string msg = std::string(tr(Str::ErrOutputDir)) + out_dir;
        fl_alert("%s", msg.c_str());
        log(std::string(tr(Str::LogInvalid)) + msg);
        return;
    }

    const std::string &exe = m_exe_path;
    const std::string &workdir = m_app_dir;
    std::string cmdline = build_command_string(exe, opt);
    log("$ " + cmdline);

    set_running(true);
    m_progress->value(0.15);
    m_progress->copy_label(tr(Str::Running));

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
    log(tr(Str::StopSent));
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
    row.choice = new Fl_Choice(0, 0, 0, 0);
    row.save = new Fl_Button(0, 0, 0, 0, tr(Str::PresetSave));
    row.rename = new Fl_Button(0, 0, 0, 0, tr(Str::PresetRename));
    row.del = new Fl_Button(0, 0, 0, 0, tr(Str::PresetDelete));

    row.choice->callback(cb_preset_choice, &row);
    // Item callbacks (installed by rebuild_choice) are what actually apply a
    // preset: they fire on every pick, the widget callback above only on a
    // change. This one is a safety net for the placeholder entry.
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
    for (Fl_Button *b : {row.save, row.rename, row.del}) {
        b->labelsize(g_m.font_base);
        b->color(kButtonBg);
    }
}

void MainWindow::layout_preset_row(const PresetRow &row, int x, int y, int w, int h) {
    if (!row.choice) return;
    // Right-aligned: the three buttons keep their size, the dropdown eats what
    // is left, so the row adapts to the column width and to the font scale.
    const int gap   = sc(8);
    const int del_w = sc(56);
    const int ren_w = sc(78);
    const int sav_w = sc(54);

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
    m_prompt_presets.items =
        load_presets(*m_settings, m_prompt_presets.key, kPromptFields);
    m_negative_presets.items =
        load_presets(*m_settings, m_negative_presets.key, kPromptFields);
    m_param_presets.items =
        load_presets(*m_settings, m_param_presets.key, kParamFields);
    for (PresetRow *row : {&m_prompt_presets, &m_negative_presets, &m_param_presets})
        row->rebuild_choice();

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
    save_presets(*m_settings, m_prompt_presets.key, m_prompt_presets.items);
    save_presets(*m_settings, m_negative_presets.key, m_negative_presets.items);
    save_presets(*m_settings, m_param_presets.key, m_param_presets.items);
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
    // The generation parameters. Output location, file name and reference images
    // stay out: those change per run, not per style.
    p.fields.emplace_back("cfg", m_cfg->value());
    p.fields.emplace_back("width", m_width->value());
    p.fields.emplace_back("height", m_height->value());
    p.fields.emplace_back("steps", m_steps->value());
    p.fields.emplace_back("seed", m_seed->value());
    p.fields.emplace_back("batch", m_batch->value());
    p.fields.emplace_back("random_seed", m_random_seed->value() ? "1" : "0");
    const char *gpu = m_gpu->text();
    p.fields.emplace_back("gpu", gpu ? gpu : "auto");
    return p;
}

void MainWindow::apply_default_values(PresetRow &row) {
    // The built-in Default entry. Everything here is compiled into the program
    // (see kDef* in the anonymous namespace).
    if (row.key == "prompt_preset")     { m_prompt_buf->text(""); return; }
    if (row.key == "negative_preset")   { m_negative_buf->text(""); return; }
    m_cfg->value(kDefCfg);
    m_width->value(kDefWidth);
    m_height->value(kDefHeight);
    m_steps->value(kDefSteps);
    m_seed->value(kDefSeed);
    m_batch->value(kDefBatch);
    m_random_seed->value(0);
    m_gpu->value(kDefGpu);
}

void MainWindow::apply_preset(PresetRow &row, int index) {
    if (index >= (int)row.items.size()) return;

    // Index -1 is the built-in Default entry, not a user preset.
    if (index < 0) {
        apply_default_values(row);
        log(std::string(tr(Str::PresetApplied)) + tr(Str::PresetDefault));
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
            else if (kv.first == "width")       m_width->value(v);
            else if (kv.first == "height")      m_height->value(v);
            else if (kv.first == "steps")       m_steps->value(v);
            else if (kv.first == "seed")        m_seed->value(v);
            else if (kv.first == "batch")       m_batch->value(v);
            else if (kv.first == "random_seed") m_random_seed->value(kv.second == "1");
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
    log(std::string(tr(Str::PresetApplied)) + p.name);
}

void MainWindow::save_current_as_preset(PresetRow &row) {
    Preset p = capture_preset(row);

    // Saving never overwrites. A preset is a template the user relies on, and
    // Save sits right next to Delete: one stray click must not silently replace
    // a preset that took effort to tune. The name starts empty and an existing
    // one is refused, so the only ways to change a preset are to delete it and
    // save again, or to store the change under a new name.
    std::string name;
    if (!ask_preset_name(tr(Str::PresetSaveTitle), tr(Str::PresetNameLabel),
                         tr(Str::PickerOk), name))
        return;

    for (const Preset &existing : row.items) {
        if (existing.name == name) {
            fl_alert("%s", tr(Str::PresetNameTaken));
            return;
        }
    }

    p.name = name;
    row.items.push_back(p);
    row.rebuild_choice();
    row.choice->value((int)row.items.size());   // keep the saved one showing
    save_settings();   // write it through now: a crash must not lose it
    log(std::string(tr(Str::PresetSaved)) + name);
}

void MainWindow::rename_selected_preset(PresetRow &row) {
    const int sel = row.picked();
    if (sel < 0) { fl_alert("%s", tr(Str::PresetNeedSelection)); return; }

    std::string name = row.items[sel].name;
    if (!ask_preset_name(tr(Str::PresetRenameTitle), tr(Str::PresetNameLabel),
                         tr(Str::PickerOk), name))
        return;

    for (size_t i = 0; i < row.items.size(); ++i) {
        if ((int)i != sel && row.items[i].name == name) {
            fl_alert("%s", tr(Str::PresetNameTaken));
            return;
        }
    }

    row.items[sel].name = name;
    row.rebuild_choice();
    row.choice->value(sel + 1);
    save_settings();
    log(std::string(tr(Str::PresetRenamed)) + name);
}

void MainWindow::delete_selected_preset(PresetRow &row) {
    const int sel = row.picked();
    if (sel < 0) { fl_alert("%s", tr(Str::PresetNeedSelection)); return; }

    const std::string name = row.items[sel].name;
    char q[512];
    std::snprintf(q, sizeof(q), tr(Str::PresetDeleteAsk), name.c_str());
    if (fl_choice("%s", tr(Str::PickerCancel), tr(Str::PresetDelete), nullptr, q) != 1)
        return;

    row.items.erase(row.items.begin() + sel);
    row.rebuild_choice();
    save_settings();
    log(std::string(tr(Str::PresetDeleted)) + name);
}

void MainWindow::cb_preset_choice(Fl_Widget *, void *data) {
    auto *row = static_cast<PresetRow *>(data);
    static_cast<MainWindow *>(row->owner)->apply_preset(*row, row->picked());
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
        fl_alert("%s", msg.c_str());
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
void MainWindow::cb_preview(Fl_Widget *, void *d) { ((MainWindow *)d)->preview_latest_output(); }
void MainWindow::cb_add_input(Fl_Widget *, void *d) { ((MainWindow *)d)->pick_input_images(); }
void MainWindow::cb_del_input(Fl_Widget *, void *d) { ((MainWindow *)d)->remove_selected_input(); }
void MainWindow::cb_browse_output(Fl_Widget *, void *d) { ((MainWindow *)d)->pick_output_dir(); }
void MainWindow::cb_open_output_dir(Fl_Widget *, void *d) { ((MainWindow *)d)->open_output_dir(); }
void MainWindow::cb_zoom_in(Fl_Widget *, void *d) { ((MainWindow *)d)->m_viewer->zoom_in(); }
void MainWindow::cb_zoom_out(Fl_Widget *, void *d) { ((MainWindow *)d)->m_viewer->zoom_out(); }
void MainWindow::cb_zoom_fit(Fl_Widget *, void *d) { ((MainWindow *)d)->m_viewer->zoom_fit(); }
void MainWindow::cb_zoom_1to1(Fl_Widget *, void *d) { ((MainWindow *)d)->m_viewer->zoom_1to1(); }

void MainWindow::cb_language(Fl_Widget *w, void *d) {
    auto *self = (MainWindow *)d;
    auto *ch = (Fl_Choice *)w;
    Lang lang = (ch->value() == 1) ? Lang::ChineseSimplified : Lang::English;
    if (lang == i18n_language()) return;

    self->apply_language(lang);
    self->log(std::string("-- language: ") + language_name(lang) + " --");
}
