// picker.cpp - self-drawn file / directory chooser
#include "picker.h"
#include "i18n.h"
#include "image_info.h"
#include "ui_main.h"   // ui_font_base(): the dialog follows the main window's scale

#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/filename.H>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <dirent.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

// Follow the same scale as the main window so the dialog does not look out of
// place. Values below are authored for a 17 px base and scaled at draw time.
constexpr int kDesignFont = 17;

// U+1F4C1; the list shows it in front of every directory entry.
const char *kFolderIcon = "\xF0\x9F\x93\x81 ";

int p_font() { return ui_font_base(); }
int p_row()  { return ui_font_base() * 22 / 10; }
int p_pad()  { return ui_font_base() + 2; }
int p_gap()  { return ui_font_base() * 7 / 10; }
int P(int design) { return design * ui_font_base() / kDesignFont; }

bool path_is_dir(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string home_directory() {
    if (const char *h = getenv("HOME"); h && *h) return h;
    if (struct passwd *pw = getpwuid(getuid())) return pw->pw_dir;
    return "/";
}

std::string strip_trailing_slashes(std::string p) {
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

// The folder that contains `p` ("/" stays "/").
std::string parent_directory(const std::string &p) {
    const std::string s = strip_trailing_slashes(p);
    if (s == "/" || s.empty()) return "/";
    const size_t slash = s.find_last_of('/');
    if (slash == std::string::npos) return s;
    if (slash == 0) return "/";
    return s.substr(0, slash);
}

// Mount points of real block devices, for the volume dropdown. Virtual
// filesystems and the snap/loop images would only bury the useful entries.
std::vector<std::string> mount_points() {
    std::vector<std::string> out;
    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return out;

    char dev[512], dir[512], type[128];
    while (fscanf(f, "%511s %511s %127s%*[^\n]", dev, dir, type) == 3) {
        int c;
        while ((c = fgetc(f)) != '\n' && c != EOF) { }   // rest of the line

        const std::string d(dev), t(type);
        if (d.rfind("/dev/", 0) != 0) continue;         // not a block device
        if (d.rfind("/dev/loop", 0) == 0) continue;     // snap / loop image
        if (t == "squashfs" || t == "overlay") continue;
        if (std::string(dir).rfind("/boot", 0) == 0) continue;  // EFI etc.

        std::string p(dir);
        // /proc/mounts escapes spaces (and a few other bytes) as \ooo
        for (size_t pos = 0; (pos = p.find("\\040", pos)) != std::string::npos; )
            p.replace(pos, 4, " ");
        if (!p.empty()) out.push_back(p);
    }
    fclose(f);

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

} // namespace

Picker::Picker(int W, int H, const char *title, bool dir_mode, bool multi,
               const char *filter, const char *ok_label)
    : Fl_Double_Window(W, H, title),
      m_dir_mode(dir_mode),
      m_multi(multi),
      m_filter(filter ? filter : ""),
      m_ok_label(ok_label && *ok_label ? ok_label : tr(Str::PickerOk)) {
    set_modal();
    build();
    // the scaled thumbnail is owned by us
    // Closing with the window button means "cancel".
    callback([](Fl_Widget *w, void *) { w->hide(); }, nullptr);
}

Picker::~Picker() {}

// ------------------------------------------------------------- ImagePane ---

ImagePane::~ImagePane() {
    if (m_scaled) m_scaled->release();
    if (m_img) m_img->release();
}

void ImagePane::set_image(Fl_Shared_Image *img) {
    if (m_img) m_img->release();
    m_img = img;
    rescale();
    redraw();
}

void ImagePane::resize(int X, int Y, int W, int H) {
    Fl_Widget::resize(X, Y, W, H);
    rescale();
}

// Build a pre-scaled copy that fits the pane and keeps the aspect ratio.
// Drawing it unscaled afterwards avoids any stretching or cropping.
void ImagePane::rescale() {
    // release(), never delete: Fl_Shared_Image::copy() hands back a cached
    // shared image, and deleting it leaves the cache holding a dangling pointer
    // that a later lookup will gladly return again (double free).
    if (m_scaled) { m_scaled->release(); m_scaled = nullptr; }
    if (!m_img || m_img->w() <= 0 || m_img->h() <= 0) return;

    const int avail_w = std::max(1, w() - 12);
    const int avail_h = std::max(1, h() - 12);
    double sc = std::min((double)avail_w / m_img->w(),
                         (double)avail_h / m_img->h());
    if (sc > 1.0) sc = 1.0;                    // never enlarge
    const int dw = std::max(1, (int)(m_img->w() * sc + 0.5));
    const int dh = std::max(1, (int)(m_img->h() * sc + 0.5));
    m_scaled = m_img->copy(dw, dh);
}

void ImagePane::draw() {
    draw_box();
    if (!m_scaled) return;
    const int dx = x() + (w() - m_scaled->w()) / 2;
    const int dy = y() + (h() - m_scaled->h()) / 2;
    m_scaled->draw(dx, dy);
}

void Picker::build() {
    const int W = w(), H = h();
    const int pad = p_pad(), gap = p_gap(), row = p_row(), fnt = p_font();

    // ---- top row: folder label, path field, navigation buttons ----
    m_dir_label = new Fl_Box(FL_NO_BOX, pad, pad + P(9), P(72), P(24), tr(Str::PickerFolder));
    m_dir_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_dir_label->labelsize(fnt);

    const int up_w = P(78), home_w = P(100), mount_w = P(170);
    const int path_x = pad + P(78);
    const int path_w = W - path_x - pad - up_w - home_w - mount_w - 3 * gap;

    m_path = new Fl_Input(path_x, pad, path_w, row);
    m_path->textsize(fnt);
    m_path->when(FL_WHEN_ENTER_KEY_ALWAYS);
    m_path->callback(cb_path, this);

    // Volume dropdown: jump straight to another partition instead of walking up
    // through the directory tree.
    m_mount = new Fl_Choice(path_x + path_w + gap, pad, mount_w, row);
    for (const std::string &mp : mount_points()) m_mount->add(mp.c_str());
    m_mount->textsize(fnt);
    m_mount->tooltip(tr(Str::PickerVolume));
    m_mount->callback(cb_mount, this);

    m_up = new Fl_Button(path_x + path_w + gap + mount_w + gap, pad, up_w, row,
                         tr(Str::PickerUp));
    m_up->labelsize(fnt);
    m_up->callback(cb_up, this);

    m_home = new Fl_Button(path_x + path_w + gap + mount_w + gap + up_w + gap,
                           pad, home_w, row, tr(Str::PickerHome));
    m_home->labelsize(fnt);
    m_home->callback(cb_home, this);

    // ---- browsing list ----
    const int list_y = pad + row + gap;
    const int bottom_rows = m_dir_mode ? 1 : 2;
    const int list_h = H - list_y - pad - bottom_rows * row - (bottom_rows - 1) * gap - gap;

    // In file mode the list shares its row with a thumbnail pane.
    const int list_w = m_dir_mode ? (W - 2 * pad) : (W - 2 * pad) * 62 / 100;

    m_browser = new Fl_File_Browser(pad, list_y, list_w, list_h);
    m_browser->textsize(fnt);
    m_browser->color(FL_WHITE);
    m_browser->type((m_multi && !m_dir_mode) ? FL_MULTI_BROWSER : FL_HOLD_BROWSER);
    m_browser->callback(cb_browser, this);

    if (!m_dir_mode) {
        const int info_h = std::max(16, fnt + 6);
        const int pw = W - 2 * pad - list_w - gap;
        m_preview = new ImagePane(pad + list_w + gap, list_y, pw, list_h - info_h - gap);
        m_preview->box(FL_DOWN_BOX);
        m_preview->color(FL_WHITE);

        // "1920 x 1080 · PNG · 3.4 MB" for the highlighted file.
        m_preview_info = new Fl_Box(FL_NO_BOX, pad + list_w + gap,
                                    list_y + list_h - info_h, pw, info_h, "");
        m_preview_info->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        m_preview_info->labelsize(fnt);
        m_preview_info->labelcolor(FL_DARK3);
    }

    // ---- bottom: optional file-name field, then the action buttons ----
    int by = H - pad - row;
    const int ok_w = P(112), cancel_w = P(112);
    if (!m_dir_mode) {
        const int name_y = by - row - gap;
        m_name_label = new Fl_Box(FL_NO_BOX, pad, name_y + P(9), P(100), P(24), tr(Str::PickerFileName));
        m_name_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        m_name_label->labelsize(fnt);

        m_name = new Fl_Input(pad + P(106), name_y,
                              W - pad - P(106) - pad - ok_w - cancel_w - 2 * gap, row);
        m_name->textsize(fnt);
        m_name->when(FL_WHEN_ENTER_KEY_ALWAYS);
        m_name->callback(cb_name, this);
    }

    m_cancel = new Fl_Button(W - pad - cancel_w, by, cancel_w, row, tr(Str::PickerCancel));
    m_cancel->labelsize(fnt);
    m_cancel->callback(cb_cancel, this);

    // A plain button: Fl_Return_Button would draw the vintage return-arrow
    // glyph on its right edge.
    m_ok = new Fl_Button(W - pad - cancel_w - gap - ok_w, by, ok_w, row,
                         m_ok_label.c_str());
    m_ok->labelsize(fnt);
    m_ok->callback(cb_ok, this);

    end();
}

std::string Picker::full_path(const char *name) const {
    if (!name || !*name) return "";
    if (name[0] == '/') return name;
    return m_dir + "/" + name;
}

// Row text with the folder glyph stripped: the list shows "\xF0\x9F\x93\x81 name"
// for directories, which must never end up inside a path.
std::string Picker::list_name(int row) const {
    if (row <= 0 || row > (int)m_rows.size()) return "";
    return m_rows[row - 1];
}

std::string Picker::picked_name() const {
    return list_name(m_browser ? m_browser->value() : 0);
}

void Picker::enter_dir(const std::string &path) {
    std::string dir = strip_trailing_slashes(path);
    if (dir.empty() || !path_is_dir(dir)) dir = home_directory();
    m_dir = dir;

    if (m_path) m_path->value(m_dir.c_str());

    refresh_list();
    update_preview();
    sync_mount();
}

// Highlights the volume the current folder lives on, so the dropdown reflects
// where you are instead of keeping whatever was picked last.
void Picker::sync_mount() {
    if (!m_mount || m_mount->size() == 0) return;

    int best = -1;
    size_t best_len = 0;
    for (int i = 0; i < m_mount->size(); ++i) {
        const char *t = m_mount->text(i);
        if (!t) continue;
        const std::string mp(t);
        // prefix match on path boundaries, with "/" matching everything
        const bool on_boundary = mp.back() == '/' ||
                                 (m_dir.size() > mp.size() && m_dir[mp.size()] == '/');
        if (m_dir == mp ||
            (m_dir.size() > mp.size() &&
             m_dir.compare(0, mp.size(), mp) == 0 && on_boundary)) {
            if (mp.size() > best_len) { best = i; best_len = mp.size(); }
        }
    }
    // deepest match wins: /run/media/x/usb beats /
    m_mount->value(best);
    m_mount->redraw();
}

// Fills the list ourselves rather than using Fl_File_Browser::load(): that
// appends a "/" to directory names, and we want a folder glyph in front
// instead. Entries are sorted with directories first.
void Picker::refresh_list() {
    m_browser->clear();

    DIR *d = opendir(m_dir.c_str());
    if (!d) return;

    std::vector<std::string> dirs, files;
    while (struct dirent *e = readdir(d)) {
        const std::string name = e->d_name;
        if (name.empty() || name == "." || name == "..") continue;
        if (name[0] == '.') continue;                 // hidden entries

        const std::string full = m_dir + "/" + name;
        if (path_is_dir(full)) {
            dirs.push_back(name);
        } else if (!m_dir_mode) {
            if (m_filter.empty() ||
                fl_filename_match(name.c_str(), m_filter.c_str()))
                files.push_back(name);
        }
    }
    closedir(d);

    std::sort(dirs.begin(), dirs.end());
    std::sort(files.begin(), files.end());

    // The row text carries format and size, which means the plain name can no
    // longer be read back out of it - so the names live in a side table indexed
    // by row, and list_name() consults that instead.
    m_rows.clear();
    for (const std::string &n : dirs) {
        m_rows.push_back(n);
        m_browser->Fl_Browser::add((std::string(kFolderIcon) + n).c_str());
    }
    for (const std::string &n : files) {
        m_rows.push_back(n);
        const ImageInfo info = image_info(m_dir + "/" + n);
        char line[512];
        if (info.valid())
            std::snprintf(line, sizeof(line), "%s    %s   %s", n.c_str(),
                          info.format.c_str(), human_size(info.bytes).c_str());
        else
            std::snprintf(line, sizeof(line), "%s", n.c_str());
        m_browser->Fl_Browser::add(line);
    }

    m_browser->redraw();
}

// Shows a scaled thumbnail of whatever file is highlighted.
void Picker::update_preview() {
    if (!m_preview) return;

    // Load at full size; ImagePane scales it to fit without distorting it.
    Fl_Shared_Image *img = nullptr;
    std::string full;
    const std::string name = picked_name();
    if (!name.empty()) {
        full = full_path(name.c_str());
        if (!path_is_dir(full)) img = Fl_Shared_Image::get(full.c_str());
    }
    m_preview->set_image(img);   // takes ownership (may be null)

    // Resolution, format and size of whatever is highlighted - read from the
    // header, so it costs nothing even for a large picture.
    if (m_preview_info) {
        std::string text;
        if (!full.empty() && !path_is_dir(full)) text = image_info_text(full);
        m_preview_info->copy_label(text.c_str());
        m_preview_info->redraw();
    }
}

void Picker::go_up() {
    if (m_dir == "/" || m_dir.empty()) return;
    const size_t slash = m_dir.find_last_of('/');
    if (slash == std::string::npos) return;
    enter_dir(slash == 0 ? "/" : m_dir.substr(0, slash));
}

void Picker::go_home() { enter_dir(home_directory()); }

void Picker::accept() {
    m_picked.clear();

    if (m_dir_mode) {
        // Enter the highlighted folder when one is selected, otherwise take the
        // folder we are currently in.
        std::string chosen = m_dir;
        const std::string name = picked_name();
        if (!name.empty()) {
            const std::string cand = full_path(name.c_str());
            if (path_is_dir(cand)) chosen = cand;
        }
        m_picked.push_back(chosen);
        m_accepted = true;
        hide();
        return;
    }

    if (m_multi) {
        for (int i = 1; i <= m_browser->size(); ++i) {
            if (!m_browser->selected(i)) continue;
            const std::string name = list_name(i);
            if (name.empty()) continue;
            const std::string full = full_path(name.c_str());
            if (path_is_dir(full)) continue;   // folders are for navigating only
            m_picked.push_back(full);
        }
    } else {
        const std::string name = picked_name();
        if (!name.empty() && !path_is_dir(full_path(name.c_str())))
            m_picked.push_back(full_path(name.c_str()));
    }

    // Fall back to whatever the user typed in the name field.
    if (m_picked.empty() && m_name) {
        const std::string typed = m_name->value() ? m_name->value() : "";
        if (!typed.empty()) m_picked.push_back(full_path(typed.c_str()));
    }

    if (m_picked.empty()) {
        fl_alert("%s", tr(Str::ErrNoSelection));
        return;
    }

    m_accepted = true;
    hide();
}

// ------------------------------------------------------------- callbacks ---

void Picker::cb_browser(Fl_Widget *w, void *d) {
    auto *self = static_cast<Picker *>(d);
    auto *b = static_cast<Fl_File_Browser *>(w);

    if (Fl::event_clicks() > 0) {
        Fl::event_clicks(0);   // do not treat a triple click as another open
        const int sel = b->value();
        if (sel <= 0) return;
        // list_name() strips the folder glyph; using the raw row text here
        // produced paths like ".../<glyph> subdir" that never existed.
        const std::string name = self->list_name(sel);
        if (name.empty()) return;

        const std::string full = self->full_path(name.c_str());
        if (path_is_dir(full)) {
            self->enter_dir(full);      // also refreshes list and preview
        } else if (!self->m_dir_mode) {
            if (self->m_name) self->m_name->value(name.c_str());
            self->accept();
        }
        return;
    }

    if (!self->m_dir_mode && self->m_name) {
        const std::string name = self->picked_name();
        if (!name.empty() && !path_is_dir(self->full_path(name.c_str())))
            self->m_name->value(name.c_str());
    }
    self->update_preview();
}

void Picker::cb_ok(Fl_Widget *, void *d) { static_cast<Picker *>(d)->accept(); }

void Picker::cb_cancel(Fl_Widget *, void *d) { static_cast<Picker *>(d)->hide(); }

void Picker::cb_up(Fl_Widget *, void *d) { static_cast<Picker *>(d)->go_up(); }

void Picker::cb_home(Fl_Widget *, void *d) { static_cast<Picker *>(d)->go_home(); }

void Picker::cb_path(Fl_Widget *w, void *d) {
    auto *self = static_cast<Picker *>(d);
    auto *in = static_cast<Fl_Input *>(w);
    const std::string typed = in->value() ? in->value() : "";
    if (typed.empty() || !path_is_dir(typed)) {
        in->value(self->m_dir.c_str());   // reject silently, restore the real path
        return;
    }
    self->enter_dir(typed);
}

void Picker::cb_mount(Fl_Widget *w, void *d) {
    auto *self = static_cast<Picker *>(d);
    const char *mp = static_cast<Fl_Choice *>(w)->text();
    if (mp && *mp) self->enter_dir(mp);
}

void Picker::cb_name(Fl_Widget *, void *d) { static_cast<Picker *>(d)->accept(); }

// ---------------------------------------------------------------- entry ---

std::string Picker::choose_directory(const char *title, const std::string &start,
                                     const char *ok_label, std::string *last_dir) {
    Picker dlg(P(940), P(680), title, true, false, nullptr, ok_label);
    dlg.enter_dir(start);
    dlg.show();
    while (dlg.shown()) Fl::wait();

    if (last_dir) {
        // Remember the folder that CONTAINS the chosen one, so reopening the
        // dialog starts beside it. Selecting a subfolder by name and entering it
        // by double-click then leave the same starting point.
        const std::string anchor = dlg.m_picked.empty() ? dlg.m_dir : dlg.m_picked[0];
        *last_dir = parent_directory(anchor);
    }

    return dlg.m_accepted && !dlg.m_picked.empty() ? dlg.m_picked[0] : std::string();
}

std::vector<std::string> Picker::choose_files(const char *title,
                                              const std::string &start,
                                              const char *filter,
                                              bool multi,
                                              const char *ok_label,
                                              std::string *last_dir) {
    Picker dlg(P(1000), P(720), title, false, multi, filter, ok_label);
    dlg.enter_dir(start);
    dlg.show();
    while (dlg.shown()) Fl::wait();
    if (last_dir) *last_dir = dlg.m_dir;
    return dlg.m_accepted ? dlg.m_picked : std::vector<std::string>();
}
