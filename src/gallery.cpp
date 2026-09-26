// gallery.cpp - thumbnail grid, detail view and film strip
#include "gallery.h"

#include "wheel_guard.h"

#include "i18n.h"
#include "image_info.h"
#include "image_viewer.h"
#include "ui_main.h"   // ui_font_base(): the preview follows the window's scale

#include <FL/Fl.H>
#include <FL/Fl_BMP_Image.H>
#include <FL/Fl_GIF_Image.H>
#include <FL/Fl_JPEG_Image.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Scroll.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>

#include <dirent.h>
#include <sys/stat.h>

namespace {

const Fl_Color kGridBg  = fl_rgb_color(0xff, 0xff, 0xff);
const Fl_Color kCellBg  = fl_rgb_color(0xe9, 0xec, 0xf2);
const Fl_Color kAccent  = fl_rgb_color(0x2e, 0x6f, 0xd6);
const Fl_Color kText    = fl_rgb_color(0x1f, 0x24, 0x2b);
const Fl_Color kMuted   = fl_rgb_color(0x6b, 0x74, 0x84);
const Fl_Color kStripBg = fl_rgb_color(0xdf, 0xe3, 0xea);

// Grid thumbnails are decoded into this box (the cell itself is smaller, so the
// picture stays sharp on scaled desktops); the strip has its own, shorter box.
constexpr int kThumbBox = 256;

// Height of the film strip: a slice of the preview pane, half again as tall as
// the first version so the small pictures are actually recognisable.
int strip_height() { return std::max(56, ui_font_base() * 15 / 2); }

// A folder larger than this is only partially previewed. Every thumbnail costs
// memory (a few hundred KB), and nobody scrolls through thousands of them.
constexpr int kMaxImages = 500;

// Where a widget sits on the screen. Walking up the parent chain - the
// top-level window included, whose x()/y() are already screen coordinates -
// lands in the same frame of reference as Fl::event_x_root(). Widget-local
// arithmetic (event_x() - x()) breaks inside a scrolled Fl_Scroll, because
// scrolling rewrites the child's x().
void widget_origin(const Fl_Widget *w, int &ox, int &oy) {
    ox = 0;
    oy = 0;
    for (const Fl_Widget *p = w; p; p = p->parent()) {
        ox += p->x();
        oy += p->y();
    }
}

bool is_image_name(const std::string &name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot + 1);
    for (char &c : ext) c = (char)tolower((unsigned char)c);
    return ext == "png" || ext == "jpg" || ext == "jpeg" ||
           ext == "webp" || ext == "bmp" || ext == "gif";
}

// Cut a name down to what fits, with a trailing ellipsis. UTF-8 aware: whole
// code points are dropped, never half of one.
std::string elide(const std::string &s, int max_w, int font_size) {
    fl_font(FL_HELVETICA, font_size);
    if (fl_width(s.c_str()) <= max_w) return s;
    const char *dots = "\xE2\x80\xA6";   // U+2026
    std::string out = s;
    while (!out.empty() && fl_width((out + dots).c_str()) > max_w) {
        size_t n = out.size();
        do { --n; } while (n > 0 && ((unsigned char)out[n] & 0xC0) == 0x80);
        out.erase(n);
    }
    return out + dots;
}

// Decode one file into a scaled copy that fits the given box. Runs on the
// worker thread, so it only uses the concrete decoders (Fl_PNG_Image and
// friends) - never Fl_Shared_Image, whose registry is shared with the UI thread.
Fl_Image *decode_file(const std::string &path) {
    const size_t dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
    for (char &c : ext) c = (char)tolower((unsigned char)c);

    Fl_Image *img = nullptr;
    if (ext == "png")                    img = new Fl_PNG_Image(path.c_str());
    else if (ext == "jpg" || ext == "jpeg") img = new Fl_JPEG_Image(path.c_str());
    else if (ext == "bmp")               img = new Fl_BMP_Image(path.c_str());
    else if (ext == "gif")               img = new Fl_GIF_Image(path.c_str());

    if (img && (img->w() <= 0 || img->h() <= 0 || !img->data())) {
        img->release();
        img = nullptr;
    }
    return img;
}

// Scale `src` down into the box, keeping the aspect ratio. Never enlarges: a
// small picture keeps its own size and is centred in the cell instead.
Fl_RGB_Image *fit_into(Fl_Image *src, int box_w, int box_h) {
    if (!src || src->w() <= 0 || src->h() <= 0) return nullptr;
    const double s = std::min((double)box_w / src->w(), (double)box_h / src->h());
    const double use = s < 1.0 ? s : 1.0;
    const int w = std::max(1, (int)(src->w() * use + 0.5));
    const int h = std::max(1, (int)(src->h() * use + 0.5));
    Fl_Image *cp = src->copy(w, h);
    if (!cp) return nullptr;
    Fl_RGB_Image *rgb = dynamic_cast<Fl_RGB_Image *>(cp);
    if (!rgb) { cp->release(); return nullptr; }
    if (rgb->w() <= 0 || rgb->h() <= 0 || !rgb->data()) { rgb->release(); return nullptr; }
    return rgb;
}

} // namespace

// ---------------------------------------------------------------- GridPane ---

// The drawing surface inside the scroll container. One widget paints every
// cell: a few hundred child widgets would cost far more than the pictures do.
// The class lives here (not in the header) because only GalleryView sees it.
class GridPane : public Fl_Widget {
public:
    GridPane(int X, int Y, int W, int H, GalleryView *owner)
        : Fl_Widget(X, Y, W, H, nullptr), m_owner(owner) { box(FL_NO_BOX); }

    int handle(int event) override;
    void draw() override;

private:
    int cell_at(int lx, int ly) const;

    GalleryView *m_owner = nullptr;
    int m_pressed = -1;
};

int GridPane::cell_at(int lx, int ly) const {
    const GalleryView *g = m_owner;
    if (lx < 0 || ly < 0 || g->m_items.empty()) return -1;
    const int cw = g->m_cell_w + g->m_gap;
    const int ch = g->m_cell_h + g->m_gap;
    const int col = (lx - g->m_gap) / cw;
    const int row = (ly - g->m_gap) / ch;
    if (col < 0 || col >= g->m_cols || row < 0) return -1;
    const int idx = row * g->m_cols + col;
    if (idx < 0 || idx >= (int)g->m_items.size()) return -1;
    return idx;
}

int GridPane::handle(int event) {
    switch (event) {
        case FL_PUSH:
            m_pressed = cell_at(Fl::event_x() - x(), Fl::event_y() - y());
            return m_pressed >= 0 ? 1 : 0;
        case FL_RELEASE: {
            const int idx = cell_at(Fl::event_x() - x(), Fl::event_y() - y());
            const bool same = (idx >= 0 && idx == m_pressed);
            m_pressed = -1;
            if (same) { m_owner->open(idx); return 1; }
            return 0;
        }
        default:
            break;
    }
    return Fl_Widget::handle(event);
}

void GridPane::draw() {
    GalleryView *g = m_owner;
    fl_color(kGridBg);
    fl_rectf(x(), y(), w(), h());

    if (g->m_items.empty()) {
        fl_color(kMuted);
        fl_font(FL_HELVETICA, std::max(12, ui_font_base() + 1));
        const char *msg = g->m_error.empty() ? tr(Str::GalleryEmpty) : g->m_error.c_str();
        fl_draw(msg, x() + 8, y(), w() - 16, h(),
                FL_ALIGN_CENTER | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
        return;
    }

    // The canvas can be many screens tall, so only the cells that touch the
    // visible part of the scrolled area are painted.
    fl_push_clip(x(), y(), w(), h());

    Fl_Scroll *sc = g->m_grid_scroll;
    const int vx0 = sc->x() - x();
    const int vy0 = sc->y() - y();
    const int vx1 = vx0 + sc->w();
    const int vy1 = vy0 + sc->h();

    const int cw = g->m_cell_w, chh = g->m_cell_h, gap = g->m_gap;
    const int cols = std::max(1, g->m_cols);
    const int n = (int)g->m_items.size();
    const int rows = (n + cols - 1) / cols;

    const int row0 = std::max(0, (vy0 - gap) / (chh + gap));
    const int row1 = std::min(rows - 1, vy1 / (chh + gap));
    const int col0 = std::max(0, (vx0 - gap) / (cw + gap));
    const int col1 = std::min(cols - 1, vx1 / (cw + gap));

    const int fsize = std::max(9, ui_font_base() * 4 / 5);
    const int label_h = g->m_cell_h - g->m_img_h;

    for (int r = row0; r <= row1; ++r) {
        for (int c = col0; c <= col1; ++c) {
            const int i = r * cols + c;
            if (i >= n) break;

            const int ix = x() + gap + c * (cw + gap);
            const int iy = y() + gap + r * (chh + gap);
            const GalleryItem &it = g->m_items[i];
            const bool selected = (i == g->m_current);

            // The thumbnail was decoded to fit this cell (see the worker), so
            // it is centred here rather than scaled: no cell can reach into
            // its neighbour.
            if (it.thumb) {
                it.thumb->draw(ix + (cw - it.thumb->w()) / 2,
                               iy + (g->m_img_h - it.thumb->h()) / 2);
            } else {
                // Decoding is still in flight (or the file is not a format we
                // can read). Either way the cell stays clickable.
                fl_color(kCellBg);
                fl_rectf(ix, iy, cw, g->m_img_h);
                fl_color(kMuted);
                fl_font(FL_HELVETICA, fsize);
                fl_draw("\xE2\x80\xA6", ix, iy, cw, g->m_img_h, FL_ALIGN_CENTER);
            }

            if (selected) {
                fl_color(kAccent);
                fl_rect(ix - 2, iy - 2, cw + 4, g->m_img_h + 4);
                fl_rect(ix - 1, iy - 1, cw + 2, g->m_img_h + 2);
            }

            fl_color(selected ? kAccent : kMuted);
            fl_font(FL_HELVETICA, fsize);
            const std::string label = elide(it.name, cw, fsize);
            fl_draw(label.c_str(), ix, iy + g->m_img_h, cw, label_h,
                    FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
        }
    }
    fl_pop_clip();
}

// --------------------------------------------------------------- FilmStrip ---

// The navigation bar under the detail view: the same list, one small picture
// per entry, click to jump. It lives inside its own scroll container.
class FilmStrip : public Fl_Widget {
public:
    FilmStrip(int X, int Y, int W, int H, GalleryView *owner)
        : Fl_Widget(X, Y, W, H, nullptr), m_owner(owner) { box(FL_NO_BOX); }

    int handle(int event) override;
    void draw() override;

private:
    int index_at(int lx, int ly) const;

    GalleryView *m_owner = nullptr;
    // A press is either a click (open that picture) or the start of a drag
    // (scroll the row). Which one it turns out to be is decided by whether the
    // pointer travels far enough before the button comes up.
    int  m_press_index = -1;
    int  m_press_x = 0;
    int  m_press_offset = 0;
    bool m_dragging = false;

    // Scrolling one notch by accident used to flip a picture; the wheel has to
    // travel two units in quick succession to count as a request.
    int    m_wheel_accum = 0;
    double m_wheel_time = 0.0;
};

int FilmStrip::index_at(int lx, int ly) const {
    const GalleryView *g = m_owner;
    if (lx < 0 || ly < 0) return -1;
    for (size_t i = 0; i < g->m_items.size(); ++i) {
        if (i + 1 >= g->m_strip_x.size()) break;
        const int x0 = g->m_strip_x[i];
        const int x1 = x0 + g->m_strip_w[i];
        if (lx >= x0 && lx < x1) return (int)i;
    }
    return -1;
}

int FilmStrip::handle(int event) {
    switch (event) {
        case FL_PUSH:
            if (Fl::event_button() != 1) break;
            m_press_index = index_at(Fl::event_x() - x(), Fl::event_y() - y());
            m_press_x = Fl::event_x();
            m_press_offset = m_owner->m_strip_scroll->xposition();
            m_dragging = false;
            // Swallow the press so the drag events that may follow arrive here
            // rather than being claimed by the scroll container.
            return 1;

        case FL_DRAG: {
            // Dragging works anywhere in the strip, including the gaps between
            // the thumbnails - a press that starts on nothing is still a reason
            // to scroll the row.
            if (Fl::event_button() != 1) break;
            const int dx = m_press_x - Fl::event_x();   // dragging left moves right
            if (!m_dragging && dx > -5 && dx < 5) break;   // still just a click
            m_dragging = true;
            m_owner->m_strip_scroll->scroll_to(std::max(0, m_press_offset + dx), 0);
            return 1;
        }

        case FL_RELEASE:
            if (Fl::event_button() != 1) break;
            if (m_press_index >= 0 && !m_dragging)
                m_owner->open(m_press_index, /*from_strip=*/true);
            m_press_index = -1;
            m_dragging = false;
            return 1;

        case FL_MOUSEWHEEL: {
            // The wheel flips through the list one picture at a time - that is
            // what the strip is for, and the row follows the selection on its
            // own. It only counts while the pointer is really over the strip:
            // FLTK passes wheel events between widgets, and stealing them from
            // the log view or the parameter fields would be worse than useless.
            if (!Fl::event_inside(this)) return 0;
            const int dy = Fl::event_dy();
            if (dy == 0) return 1;

            // A single notch is far too easy to trigger by accident, so the
            // wheel has to travel two units in the same direction within a
            // short time before anything happens. Reversing resets the count.
            const double now = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (now - m_wheel_time > 0.4 || (m_wheel_accum > 0) != (dy > 0))
                m_wheel_accum = 0;
            m_wheel_accum += dy;
            m_wheel_time = now;
            if (std::abs(m_wheel_accum) < 2) return 1;

            m_owner->step(m_wheel_accum > 0 ? +1 : -1);
            m_wheel_accum = 0;
            return 1;
        }

        default:
            break;
    }
    // Everything else is left to the Fl_Scroll around us.
    return Fl_Widget::handle(event);
}

void FilmStrip::draw() {
    GalleryView *g = m_owner;
    fl_color(kStripBg);
    fl_rectf(x(), y(), w(), h());

    if (g->m_items.empty()) {
        fl_color(kMuted);
        fl_font(FL_HELVETICA, std::max(10, ui_font_base() * 4 / 5));
        fl_draw(tr(Str::GalleryEmpty), x() + 6, y(), w() - 12, h(),
                FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        return;
    }

    fl_push_clip(x(), y(), w(), h());

    const int base = ui_font_base();
    const int pad = std::max(4, base / 4);
    const int ih = std::max(16, h() - 2 * pad);

    for (size_t i = 0; i < g->m_items.size(); ++i) {
        if (i + 1 >= g->m_strip_x.size()) break;
        const int ix = x() + g->m_strip_x[i];
        const int iw = g->m_strip_w[i];
        if (ix > x() + w() || ix + iw < x()) continue;

        const GalleryItem &it = g->m_items[i];
        const int iy = y() + (h() - ih) / 2;
        const bool selected = ((int)i == g->m_current);

        if (it.strip) {
            it.strip->draw(ix + (iw - it.strip->w()) / 2,
                           iy + (ih - it.strip->h()) / 2);
        } else {
            fl_color(kCellBg);
            fl_rectf(ix, iy, iw, ih);
        }

        fl_color(selected ? kAccent : fl_rgb_color(0xb6, 0xbd, 0xc9));
        fl_rect(ix, iy, iw, ih);
        if (selected) fl_rect(ix - 1, iy - 1, iw + 2, ih + 2);
    }
    fl_pop_clip();
}

// ------------------------------------------------------------- GalleryView ---

GalleryView::GalleryView(int X, int Y, int W, int H, const char *L)
    : Fl_Group(X, Y, W, H, L) {
    box(FL_FLAT_BOX);
    color(kGridBg);

    // ---- grid ----
    m_grid_scroll = new GuardedScroll(X, Y, W, H);
    m_grid_scroll->box(FL_FLAT_BOX);
    m_grid_scroll->type(Fl_Scroll::BOTH);
    m_grid_scroll->color(kGridBg);
    m_grid_scroll->scrollbar_size(0);   // follow the global size, like the log view
    m_grid = new GridPane(X, Y, W, H, this);
    m_grid_scroll->end();

    // ---- detail: the picture on top, the film strip under it ----
    m_detail = new Fl_Group(X, Y, W, H);
    m_viewer = new ImageViewer(X, Y, W, H * 3 / 4);
    m_info = new Fl_Box(FL_NO_BOX, X, Y, W, 18, "");
    m_info->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    m_info->labelsize(std::max(9, ui_font_base() - 1));
    m_info->labelcolor(kMuted);
    m_strip_scroll = new GuardedScroll(X, Y + H * 3 / 4, W, H / 4);
    m_strip_scroll->box(FL_FLAT_BOX);
    m_strip_scroll->type(Fl_Scroll::HORIZONTAL);
    m_strip_scroll->color(kStripBg);
    m_strip_scroll->scrollbar_size(0);
    m_strip = new FilmStrip(X, Y + H * 3 / 4, W, H / 4, this);
    m_strip_scroll->end();
    m_detail->end();
    m_detail->hide();   // the grid is what the window opens on

    end();

    // Cell metrics are fixed for the life of the window: they follow the font
    // scale, which never changes at run time. The worker therefore decodes
    // thumbnails straight into the size the grid draws them at. Letting the
    // grid scale them at draw time is what made oversized thumbnails bleed
    // over their neighbours into a jumble.
    const int base = ui_font_base();
    m_cell_w = std::max(90, base * 11);
    m_img_h  = std::max(60, base * 8);
    m_cell_h = m_img_h + std::max(16, base * 13 / 10);
    m_gap    = std::max(6, base / 2);

    m_strip_x.assign(1, 0);
    m_strip_thumb_h = std::max(16, strip_height()
                                       - 2 * std::max(4, base / 4) - 2);
    layout_grid_canvas();
    start_worker();
}

GalleryView::~GalleryView() {
    stop_worker();
    drop_items();
}

void GalleryView::start_worker() {
    m_worker = std::thread([this] { worker_loop(); });
}

void GalleryView::stop_worker() {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_quit = true;
        m_jobs.clear();
    }
    m_cv.notify_all();
    if (m_worker.joinable()) m_worker.join();
}

// ---------------------------------------------------------------- scanning ---

void GalleryView::set_directory(const std::string &dir) {
    if (dir == m_dir) return;
    m_dir = dir;
    refresh();
}

void GalleryView::drop_items() {
    for (GalleryItem &it : m_items) {
        if (it.thumb) { it.thumb->release(); it.thumb = nullptr; }
        if (it.strip) { it.strip->release(); it.strip = nullptr; }
    }
    m_items.clear();
}

void GalleryView::scan() {
    const std::string keep = current_path();

    drop_items();
    m_error.clear();
    ++m_generation;          // results still in flight belong to the old list
    m_current = -1;

    DIR *d = opendir(m_dir.c_str());
    if (!d) {
        m_error = std::string(tr(Str::GalleryUnreadable)) + m_dir;
        return;
    }

    struct Entry { std::string path, name; long long mtime; };
    std::vector<Entry> found;
    while (struct dirent *e = readdir(d)) {
        const std::string name = e->d_name;
        if (name.empty() || name[0] == '.') continue;
        if (!is_image_name(name)) continue;
        const std::string path = m_dir + "/" + name;
        struct stat st;
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        found.push_back({path, name, (long long)st.st_mtime});
    }
    closedir(d);

    // Newest first: the picture a run just wrote is the one the user looks for.
    // Equal timestamps (a batch writes several files within the same second)
    // fall back to the name, so the order is stable across refreshes.
    std::sort(found.begin(), found.end(), [](const Entry &a, const Entry &b) {
        if (a.mtime != b.mtime) return a.mtime > b.mtime;
        return a.name > b.name;
    });

    if ((int)found.size() > kMaxImages) found.resize(kMaxImages);

    m_items.reserve(found.size());
    for (Entry &f : found)
        m_items.push_back(GalleryItem{f.path, f.name, f.mtime, nullptr, nullptr});

    // Keep showing the picture that was open, when it survived the rescan.
    if (!keep.empty()) {
        for (size_t i = 0; i < m_items.size(); ++i) {
            if (m_items[i].path == keep) { m_current = (int)i; break; }
        }
    }
}

void GalleryView::queue_jobs() {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_jobs.clear();
        for (size_t i = 0; i < m_items.size(); ++i)
            m_jobs.push_back(Job{(int)i, m_items[i].path});
    }
    m_pending = (int)m_items.size();
    m_cv.notify_all();
}

void GalleryView::refresh() {
    scan();
    queue_jobs();

    if (m_items.empty()) {
        m_viewer->clear();
        if (m_detail) update_info();
        if (m_mode == Mode::Detail) set_mode(Mode::Grid);
    }
    relayout();
    m_grid->redraw();
    m_strip->redraw();
    redraw();
}

// "name · 1024x1024 · PNG · 1.2 MB" for the picture on screen.
void GalleryView::update_info() {
    if (!m_info) return;
    if (m_current < 0 || m_current >= (int)m_items.size()) {
        m_info->copy_label("");
        m_info->redraw();
        return;
    }
    const GalleryItem &it = m_items[m_current];
    const std::string facts = image_info_text(it.path);
    const std::string text = facts.empty() ? it.name : (it.name + "     " + facts);
    m_info->copy_label(text.c_str());
    m_info->redraw();
}

std::string GalleryView::current_path() const {
    if (m_current < 0 || m_current >= (int)m_items.size()) return "";
    return m_items[m_current].path;
}

int GalleryView::thumbs_loaded() const {
    int n = 0;
    for (const GalleryItem &it : m_items)
        if (it.thumb) ++n;
    return n;
}

// -------------------------------------------------------------- thumbnails ---

void GalleryView::worker_loop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_cv.wait(lk, [this] { return m_quit || !m_jobs.empty(); });
            if (m_jobs.empty()) {
                if (m_quit) return;
                continue;
            }
            job = m_jobs.front();
            m_jobs.pop_front();
        }

        Thumb t;
        t.index = job.index;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            t.generation = m_generation;
        }

        // One decode feeds both thumbnails. The grid copy is sized to the cell
        // it will be drawn in, so nothing has to be scaled or clipped later.
        Fl_Image *src = decode_file(job.path);
        if (src) {
            const int pad = 3;
            t.grid  = fit_into(src, std::max(16, m_cell_w - 2 * pad),
                                      std::max(16, m_img_h - 2 * pad));
            t.strip = fit_into(src, kThumbBox * 2, std::max(16, m_strip_thumb_h));
            src->release();
        }

        // Push the result even when nothing could be decoded: the main thread
        // counts the answers to know when the queue has drained, so a file it
        // cannot read must not leave that count stuck.
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_ready.push_back(t);
        }
        Fl::awake(&GalleryView::on_thumb_ready, this);
    }
}

void GalleryView::release_thumb(Thumb &t) {
    if (t.grid) { t.grid->release(); t.grid = nullptr; }
    if (t.strip) { t.strip->release(); t.strip = nullptr; }
}

void GalleryView::on_thumb_ready(void *data) {
    static_cast<GalleryView *>(data)->collect_thumbs();
}

void GalleryView::collect_thumbs() {
    std::vector<Thumb> ready;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        ready.swap(m_ready);
    }
    if (ready.empty()) return;

    bool layout_dirty = false;
    for (Thumb &t : ready) {
        if (m_pending > 0) --m_pending;

        // A refresh() happened while this one was decoding: it belongs to a
        // list that no longer exists.
        if (t.generation != m_generation ||
            t.index < 0 || t.index >= (int)m_items.size()) {
            release_thumb(t);
            continue;
        }

        GalleryItem &it = m_items[t.index];
        if (it.thumb) it.thumb->release();
        if (it.strip) it.strip->release();
        it.thumb = t.grid;
        it.strip = t.strip;
        t.grid = t.strip = nullptr;
        if (it.strip) layout_dirty = true;
    }

    if (layout_dirty) layout_strip_canvas();
    m_grid->redraw();
    m_strip->redraw();
}

// ----------------------------------------------------------------- layout ---

void GalleryView::layout_grid_canvas() {
    // Only the column count depends on the pane size; the cell size itself was
    // settled when the window was built (see the constructor).
    const int sb = std::max(1, Fl::scrollbar_size());
    const int vw = std::max(1, m_grid_scroll->w() - sb);
    const int vh = std::max(1, m_grid_scroll->h() - sb);

    m_cols = std::max(1, (vw - m_gap) / (m_cell_w + m_gap));
    const int rows = (int)((m_items.size() + m_cols - 1) / m_cols);
    const int need_w = m_gap + m_cols * (m_cell_w + m_gap);
    const int need_h = m_gap + rows * (m_cell_h + m_gap);

    // Keep whatever the user has scrolled to. Fl_Scroll scrolls by moving its
    // child, so putting the canvas back at the viewport origin here would undo
    // the scrolling on every relayout - a window resize, a late thumbnail, or
    // opening a picture from the film strip.
    const int off_x = m_grid->x() - m_grid_scroll->x();
    const int off_y = m_grid->y() - m_grid_scroll->y();
    m_grid->resize(m_grid_scroll->x() + off_x, m_grid_scroll->y() + off_y,
                   std::max(vw, need_w), std::max(vh, need_h));
}

void GalleryView::layout_strip_canvas() {
    const int base = ui_font_base();
    const int pad = std::max(4, base / 4);
    const int gap = pad;
    const int h = m_strip_scroll->h();

    const size_t n = m_items.size();
    m_strip_x.assign(n + 1, pad);
    m_strip_w.assign(n, m_strip_thumb_h);

    int x = pad;
    for (size_t i = 0; i < n; ++i) {
        const int w = m_items[i].strip ? m_items[i].strip->w() : m_strip_thumb_h;
        m_strip_w[i] = w;
        m_strip_x[i] = x;
        x += w + gap;
    }
    m_strip_x[n] = x;

    const int canvas_w = std::max(m_strip_scroll->w(), x + pad);

    // Same as the grid: leave the strip where the user scrolled it. Re-setting
    // the child's position to the viewport origin is what made the row jump
    // back to the start after a click.
    const int off_x = m_strip->x() - m_strip_scroll->x();
    const int off_y = m_strip->y() - m_strip_scroll->y();
    m_strip->resize(m_strip_scroll->x() + off_x, m_strip_scroll->y() + off_y,
                    canvas_w, h);
}

void GalleryView::relayout() {
    if (m_mode == Mode::Grid) {
        m_grid_scroll->resize(x(), y(), w(), h());
        layout_grid_canvas();
    } else {
        const int strip_h = strip_height();
        const int info_h = std::max(16, ui_font_base() + 6);
        const int viewer_h = std::max(80, h() - strip_h - info_h - 4);
        m_detail->resize(x(), y(), w(), h());
        m_viewer->resize(x(), y(), w(), viewer_h);
        m_info->resize(x(), y() + viewer_h + 2, w(), info_h);
        m_strip_scroll->resize(x(), y() + viewer_h + info_h + 4, w(), strip_h);
        layout_strip_canvas();
    }
}

void GalleryView::apply_mode_visibility() {
    // Exactly one of the two views is visible. Keeping this in one place also
    // covers Fl_Group::show(): showing a window walks every child and shows it,
    // which would otherwise put the hidden view back on screen.
    if (m_mode == Mode::Grid) {
        m_detail->hide();
        m_grid_scroll->show();
    } else {
        m_grid_scroll->hide();
        m_detail->show();
    }
}

void GalleryView::show() {
    Fl_Group::show();
    apply_mode_visibility();
}

void GalleryView::resize(int X, int Y, int W, int H) {
    Fl_Group::resize(X, Y, W, H);
    if (m_grid_scroll) relayout();
}

void GalleryView::scroll_grid_to_current() {
    if (m_current < 0 || m_cols < 1) return;
    const int row = m_current / m_cols;
    const int y0 = m_gap + row * (m_cell_h + m_gap);

    // The canvas is a child of the scroll, so its own y() is the negated scroll
    // offset. Reading it is equivalent to yposition(), but it cannot disagree
    // with where the cells are actually drawn.
    const int offset = m_grid_scroll->yposition();
    const int vh = m_grid_scroll->h();
    int target = offset;
    if (y0 - m_gap < offset) target = y0 - m_gap;
    else if (y0 + m_cell_h + m_gap > offset + vh) target = y0 + m_cell_h + m_gap - vh;
    target = std::max(0, target);
    if (target != offset)
        m_grid_scroll->scroll_to(m_grid_scroll->xposition(), target);
}

void GalleryView::scroll_strip_to_current() {
    if (m_current < 0 || m_current + 1 >= (int)m_strip_x.size()) return;
    const int pad = std::max(4, ui_font_base() / 4);
    const int x0 = m_strip_x[m_current];
    const int x1 = x0 + m_strip_w[m_current];

    // Same reasoning as the grid: the strip's own x() is the negated offset, and
    // it is what the drawing uses, so the two can never drift apart.
    const int offset = m_strip_scroll->xposition();
    const int vw = m_strip_scroll->w();

    int target = offset;
    if (x0 - pad < offset) target = x0 - pad;
    else if (x1 + pad > offset + vw) target = x1 + pad - vw;
    target = std::max(0, target);
    if (target != offset)
        m_strip_scroll->scroll_to(target, 0);
}

// -------------------------------------------------------------- navigation ---

void GalleryView::set_mode(Mode m) {
    if (m_mode == m) return;
    m_mode = m;

    apply_mode_visibility();

    relayout();
    if (m == Mode::Grid) scroll_grid_to_current();
    else                scroll_strip_to_current();

    if (on_mode_change) on_mode_change();
    redraw();
}

void GalleryView::open(int index, bool from_strip) {
    if (index < 0 || index >= (int)m_items.size()) return;
    if (index == m_current && m_mode == Mode::Detail) return;

    m_current = index;

    // Switch the view *before* loading. Loading fits the picture to the viewer's
    // current size, and while the grid is on screen the detail view has not
    // been laid out yet - it still carries the size it was built with.
    // Loading first therefore fitted the picture to a zero-sized pane, which is
    // the blank image the first click used to show.
    if (m_mode != Mode::Detail) {
        set_mode(Mode::Detail);          // relayouts and scrolls the strip
    } else {
        // Deliberately no relayout here. The strip's item positions only change
        // when a thumbnail arrives, and collect_thumbs() re-lays it out then;
        // redoing it on every click is what let the row jump.
        scroll_strip_to_current();
    }

    const std::string &path = m_items[index].path;
    if (!m_viewer->load(path) && on_error) on_error(m_viewer->error());
    update_info();

    m_grid->redraw();
    m_strip->redraw();
}

void GalleryView::show_latest() {
    if (m_items.empty()) return;
    open(0);   // the list is sorted newest first
}

void GalleryView::step(int delta) {
    if (m_items.empty() || delta == 0) return;
    const int last = (int)m_items.size() - 1;
    const int from = m_current < 0 ? 0 : m_current;
    const int next = std::min(std::max(from + delta, 0), last);
    if (next == m_current) return;
    open(next);      // loads the picture, redraws both views, scrolls the strip
}

void GalleryView::select(int index) {
    if (index < 0 || index >= (int)m_items.size()) return;
    if (index == m_current) return;
    m_current = index;
    m_grid->redraw();
    m_strip->redraw();
}

void GalleryView::zoom_in()  { if (m_mode == Mode::Detail) m_viewer->zoom_in(); }
void GalleryView::zoom_out() { if (m_mode == Mode::Detail) m_viewer->zoom_out(); }
void GalleryView::zoom_fit() { if (m_mode == Mode::Detail) m_viewer->zoom_fit(); }
void GalleryView::zoom_1to1(){ if (m_mode == Mode::Detail) m_viewer->zoom_1to1(); }
