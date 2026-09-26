#include "image_viewer.h"
#include "image_info.h"
#include "wheel_guard.h"

#include <FL/Fl.H>
#include <FL/Fl_Image.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_JPEG_Image.H>
#include <FL/Fl_BMP_Image.H>
#include <FL/Fl_GIF_Image.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Window.H>

#include <algorithm>
#include <cctype>
#include <string>
#include <cmath>
#include <vector>

#ifdef HAVE_LIBPNG
#include <png.h>
// FLTK's Fl_PNG_Image cannot read RGBA (transparent) PNGs produced by
// qwenimage-ncnn-vulkan properly in all versions, so decode with libpng and
// hand back plain RGBA at full size. Transparency is composited later, in one
// place that serves every format.
static Fl_RGB_Image *load_png_rgba(const std::string &path, std::string &err) {
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) { err = "cannot open file"; return nullptr; }
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) { fclose(fp); err = "png_create_read_struct failed"; return nullptr; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, nullptr, nullptr); fclose(fp); err = "png_create_info_struct failed"; return nullptr; }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        err = "libpng error while decoding";
        return nullptr;
    }
    png_init_io(png, fp);
    png_read_info(png, info);

    png_uint_32 w = png_get_image_width(png, info);
    png_uint_32 h = png_get_image_height(png, info);
    int color_type = png_get_color_type(png, info);
    int bit_depth = png_get_bit_depth(png, info);

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    if (!(color_type & PNG_COLOR_MASK_ALPHA)) png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);

    png_read_update_info(png, info);

    std::vector<png_bytep> rows(h);
    std::vector<unsigned char> raw((size_t)w * 4 * h);
    for (png_uint_32 y = 0; y < h; ++y) rows[y] = raw.data() + (size_t)y * w * 4;
    png_read_image(png, rows.data());
    png_read_end(png, nullptr);
    png_destroy_read_struct(&png, &info, nullptr);
    fclose(fp);

    // copy() hands back a picture that owns its bytes, which the vector above
    // does not, so the decode can go out of scope safely.
    Fl_RGB_Image *tmp = new Fl_RGB_Image(raw.data(), (int)w, (int)h, 4);
    Fl_RGB_Image *copy = dynamic_cast<Fl_RGB_Image *>(tmp->copy((int)w, (int)h));
    delete tmp;
    return copy;
}
#endif

// Hand an image back where it came from. release() is "delete this" for a
// plain image and drops a reference for a cached one, so it is the one call
// that is always right - deleting a shared image directly would leave FLTK's
// cache pointing at freed memory.
static void release_image(Fl_Image *&img) {
    if (img) img->release();
    img = nullptr;
}

// Lowercase file extension without the dot, "" when there is none.
static std::string lower_ext(const std::string &path) {
    const size_t dot = path.rfind('.');
    const size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    std::string ext = path.substr(dot + 1);
    for (char &c : ext) c = (char)std::tolower((unsigned char)c);
    return ext;
}

// The pixels behind whatever a loader handed back. Fl_Shared_Image wraps the
// concrete decoder, so unwrap it first. The bytes live in Fl_RGB_Image::array;
// Fl_Image::data() is a *row table* pointer, not the picture, and reading it as
// pixels walks off into nothing.
static Fl_RGB_Image *rgb_pixels(Fl_Image *img) {
    if (!img) return nullptr;
    if (Fl_RGB_Image *rgb = dynamic_cast<Fl_RGB_Image *>(img)) return rgb;
    if (Fl_Shared_Image *si = dynamic_cast<Fl_Shared_Image *>(img)) {
        const Fl_Image *inner = si->image();
        return inner ? dynamic_cast<Fl_RGB_Image *>(const_cast<Fl_Image *>(inner)) : nullptr;
    }
    return nullptr;
}

// Anything with an alpha channel is composited over the classic grey
// checkerboard, so a transparent background reads as transparent instead of as
// whatever colour happens to sit behind the window.
static Fl_RGB_Image *checkerboard_over(Fl_RGB_Image *rgb) {
    if (!rgb) return nullptr;
    const int w = rgb->w(), h = rgb->h(), d = rgb->d();
    const uchar *in = rgb->array;
    if (!in || d != 4 || w <= 0 || h <= 0) return nullptr;
    const int ld = rgb->ld() > 0 ? rgb->ld() : w * d;

    static const int kTile = 12;
    std::vector<unsigned char> out((size_t)w * 3 * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uchar *s = in + (size_t)y * ld + (size_t)x * d;
            const unsigned char bg = (((x / kTile) + (y / kTile)) & 1) ? 0xFF : 0xDD;
            unsigned char *o = out.data() + ((size_t)y * w + x) * 3;
            const unsigned int a = s[3];
            o[0] = (unsigned char)((s[0] * a + bg * (255 - a)) / 255);
            o[1] = (unsigned char)((s[1] * a + bg * (255 - a)) / 255);
            o[2] = (unsigned char)((s[2] * a + bg * (255 - a)) / 255);
        }
    }
    // copy() gives a picture that owns its own bytes, which the vector above
    // does not.
    Fl_RGB_Image *tmp = new Fl_RGB_Image(out.data(), w, h, 3);
    Fl_RGB_Image *copy = dynamic_cast<Fl_RGB_Image *>(tmp->copy(w, h));
    delete tmp;
    return copy;
}

// Decode one file, optionally no larger than the given box. A cap keeps the
// wheel flips cheap: the full file is only pulled in when somebody zooms past
// what the small copy can show. 0 means "no cap".
static Fl_Image *decode_image(const std::string &path, int cap_w, int cap_h, std::string &err) {
    Fl_Image *img = nullptr;
    Fl_Shared_Image *shared = nullptr;

    if (lower_ext(path) == "png") {
#ifdef HAVE_LIBPNG
        img = load_png_rgba(path, err);
#endif
    }

    if (!img) {
        // FLTK's own decoders can go straight to a smaller size, which is far
        // cheaper than loading the whole file and scaling afterwards.
        if (cap_w > 0 && cap_h > 0) shared = Fl_Shared_Image::get(path.c_str(), cap_w, cap_h);
        else                        shared = Fl_Shared_Image::get(path.c_str());
        if (shared && (shared->w() <= 0 || shared->h() <= 0)) { shared->release(); shared = nullptr; }
        img = shared;
    }
    if (!img) return nullptr;

    // libpng has no scaled decode, so shrink here.
    if (cap_w > 0 && cap_h > 0 && (img->w() > cap_w || img->h() > cap_h)) {
        const double k = std::min((double)cap_w / img->w(), (double)cap_h / img->h());
        const int w = std::max(1, (int)(img->w() * k + 0.5));
        const int h = std::max(1, (int)(img->h() * k + 0.5));
        Fl_Image *small = img->copy(w, h);
        img->release();
        if (!small) return nullptr;
        img = small;
    }

    // Transparency gets the checkerboard treatment whatever the format, so PNG,
    // WebP and GIF all read the same way.
    Fl_RGB_Image *rgb = rgb_pixels(img);
    if (rgb && rgb->d() == 4 && rgb->array) {
        if (Fl_RGB_Image *flat = checkerboard_over(rgb)) {
            img->release();
            img = flat;
        }
    }
    return img;
}

// Fetch the file again at full size. The zoom is counted in units of the
// file's own dimensions, so nothing has to be re-fitted: swapping the source
// in only sharpens what is already on screen.
bool ImageViewer::upgrade_resolution() {
    if (m_hi_res || m_path.empty() || !m_source) return false;
    std::string err;
    Fl_Image *full = decode_image(m_path, 0, 0, err);
    if (!full) return false;
    if (full->w() <= m_source->w() && full->h() <= m_source->h()) {
        full->release();          // nothing better to be had
        m_hi_res = true;
        return false;
    }
    release_image(m_source);
    m_source = full;
    m_hi_res = true;
    return true;
}

// The surface the picture is drawn on. Dragging pans the view when the picture
// is bigger than the pane. The press is claimed here so that the Fl_Scroll
// underneath does not start a drag of its own - two owners of one offset would
// fight, and the picture would jump.
class PanCanvas : public Fl_Box {
public:
    PanCanvas(int X, int Y, int W, int H, ImageViewer *owner)
        : Fl_Box(X, Y, W, H, nullptr), m_owner(owner) {}

    int handle(int event) override {
        switch (event) {
            case FL_PUSH:
                if (Fl::event_button() != 1 || !m_owner->has_image()) break;
                m_press_x = Fl::event_x();
                m_press_y = Fl::event_y();
                m_press_ox = m_owner->m_scroll->xposition();
                m_press_oy = m_owner->m_scroll->yposition();
                m_panned = false;
                return 1;

            case FL_DRAG: {
                if (Fl::event_button() != 1) break;
                const int dx = Fl::event_x() - m_press_x;
                const int dy = Fl::event_y() - m_press_y;
                if (!m_panned && dx > -4 && dx < 4 && dy > -4 && dy < 4) break;
                m_panned = true;
                m_owner->scroll_clamped(m_press_ox - dx, m_press_oy - dy);
                return 1;
            }

            case FL_RELEASE:
                if (Fl::event_button() != 1) break;
                m_panned = false;
                return 1;

            default:
                break;
        }
        return Fl_Box::handle(event);
    }

private:
    ImageViewer *m_owner = nullptr;
    int  m_press_x = 0, m_press_y = 0;
    int  m_press_ox = 0, m_press_oy = 0;
    bool m_panned = false;
};

ImageViewer::ImageViewer(int X, int Y, int W, int H, const char *L)
    : Fl_Group(X, Y, W, H, L) {
    // One scroll child, and inside it a canvas that grows past the viewport
    // when the picture does not fit. The canvas keeps the picture centred in
    // itself, so a small picture sits in the middle of the pane.
    m_scroll = new GuardedScroll(X, Y, W, H);
    m_scroll->box(FL_FLAT_BOX);
    m_scroll->type(Fl_Scroll::BOTH);
    m_scroll->color(FL_WHITE);
    m_scroll->scrollbar_size(0);

    m_canvas = new PanCanvas(X, Y, W, H, this);
    m_canvas->box(FL_NO_BOX);
    m_canvas->align(FL_ALIGN_INSIDE | FL_ALIGN_CENTER);
    m_canvas->color(FL_WHITE);
    m_canvas->labelsize(20);
    m_canvas->labelcolor(FL_DARK3);
    m_canvas->label("(no image)");

    end();
    resizable(m_scroll);
}

ImageViewer::~ImageViewer() {
    clear();
}

bool ImageViewer::load(const std::string &path) {
    clear();
    m_path = path;

    // The picture's own size, straight from the header: the zoom counts in
    // those units, and the decode below is allowed to come in smaller.
    int ow = 0, oh = 0;
    if (!image_dimensions(path, ow, oh)) { ow = 0; oh = 0; }

    // Decode no larger than the pane can use. Flipping through pictures with
    // the wheel should not decode 2048x2048 every time; zooming in pulls the
    // full resolution on demand.
    const int cap_w = std::max(128, m_scroll->w() * 2);
    const int cap_h = std::max(128, m_scroll->h() * 2);
    std::string err;
    Fl_Image *img = decode_image(path, cap_w, cap_h, err);

    if (!img) {
        m_error = err.empty() ? ("unsupported or unreadable image: " + path) : err;
        m_canvas->image(nullptr);
        m_canvas->label(("(failed to load)\n" + m_error).c_str());
        m_canvas->redraw();
        return false;
    }

    const int dw = img->w(), dh = img->h();
    m_orig_w = ow > 0 ? ow : dw;
    m_orig_h = oh > 0 ? oh : dh;
    m_hi_res = (dw >= m_orig_w && dh >= m_orig_h);
    m_source = img;
    m_error.clear();
    zoom_fit();
    return true;
}

void ImageViewer::clear() {
    m_canvas->image(nullptr);
    if (m_scaled == m_source) m_scaled = nullptr;   // same object, free it once
    release_image(m_scaled);
    release_image(m_source);
    m_zoom = 1.0;
    m_orig_w = m_orig_h = 0;
    m_hi_res = false;
    m_error.clear();
    m_path.clear();
    m_canvas->label("(no image)");
    layout_children();
    redraw();
}

void ImageViewer::apply_zoom() {
    if (!m_source) return;

    // Asking for more pixels than the cheap decode has? Go and get the real
    // ones. The zoom does not change - it is counted in units of the file's own
    // size - so this only sharpens the picture already on screen.
    if (!m_hi_res && (double)m_orig_w * m_zoom > m_source->w() + 0.5)
        upgrade_resolution();

    release_image(m_scaled);

    const int w = std::max(1, (int)(m_orig_w * m_zoom + 0.5));
    const int h = std::max(1, (int)(m_orig_h * m_zoom + 0.5));
    m_scaled = m_source->copy(w, h);

    m_canvas->image(m_scaled);
    m_canvas->label(nullptr);
    layout_children();
    m_scroll->redraw();
    redraw();
}

// How far the view can scroll. The visible area loses the width of whichever
// scrollbar is showing, so the picture can always be brought fully into view.
void ImageViewer::scroll_limits(int &max_x, int &max_y) const {
    const int sb = scrollbar_px();
    const int view_w = m_scroll->w() - (m_canvas->h() > m_scroll->h() ? sb : 0);
    const int view_h = m_scroll->h() - (m_canvas->w() > m_scroll->w() ? sb : 0);
    max_x = std::max(0, m_canvas->w() - view_w);
    max_y = std::max(0, m_canvas->h() - view_h);
}

// Move the view, staying inside those limits: scroll_to() does not clamp by
// itself, and an out-of-range offset pushes the canvas off the viewport.
//
// Fl_Scroll's fast path blits the pixels already on screen and repaints only
// what scrolls into view. That shortcut assumes the content stands still; while
// dragging, several blits land in one frame and the seams between them show up
// as misplaced blocks. Asking for a full repaint instead keeps it exact.
void ImageViewer::scroll_clamped(int x, int y) {
    int max_x = 0, max_y = 0;
    scroll_limits(max_x, max_y);
    x = std::min(std::max(x, 0), max_x);
    y = std::min(std::max(y, 0), max_y);
    if (x == m_scroll->xposition() && y == m_scroll->yposition()) return;

    m_scroll->scroll_to(x, y);
    m_scroll->damage(FL_DAMAGE_ALL);
    m_canvas->redraw();
}

int ImageViewer::scrollbar_px() const {
    const int s = m_scroll->scrollbar_size();
    return s > 0 ? s : Fl::scrollbar_size();
}

void ImageViewer::layout_children() {
    const int iw = m_scaled ? m_scaled->w() : 0;
    const int ih = m_scaled ? m_scaled->h() : 0;

    // The canvas always covers the whole viewport, so a picture that fits is
    // centred in the widget itself. Taking the scrollbar width off both axes
    // shrank the canvas while no scrollbar was shown, and because the canvas
    // sits in the top left corner the picture came out off centre - the gap on
    // the right one scrollbar wider than the one on the left.
    const int cw = std::max(iw, m_scroll->w());
    const int ch = std::max(ih, m_scroll->h());

    // Trim the scroll offset when the canvas shrank under it, and place the
    // canvas where that offset says. Fl_Scroll moves its children by itself, so
    // a canvas pinned to the bare viewport corner while the offset is non-zero
    // gets yanked back on the next scrollbar event, taking the picture with it.
    const int sb = scrollbar_px();
    const int view_w = m_scroll->w() - (ch > m_scroll->h() ? sb : 0);
    const int view_h = m_scroll->h() - (cw > m_scroll->w() ? sb : 0);
    // (limits computed against the *new* canvas size, before it is applied)
    const int max_x = std::max(0, cw - view_w);
    const int max_y = std::max(0, ch - view_h);
    const int keep_x = std::min(std::max(m_scroll->xposition(), 0), max_x);
    const int keep_y = std::min(std::max(m_scroll->yposition(), 0), max_y);

    m_canvas->resize(m_scroll->x() - m_scroll->xposition(),
                     m_scroll->y() - m_scroll->yposition(), cw, ch);
    if (keep_x != m_scroll->xposition() || keep_y != m_scroll->yposition())
        scroll_clamped(keep_x, keep_y);
    if (iw && ih) {
        m_canvas->image(m_scaled);
    }
    m_scroll->redraw();
}

void ImageViewer::resize(int X, int Y, int W, int H) {
    Fl_Group::resize(X, Y, W, H);
    if (m_scroll) {
        m_scroll->resize(X, Y, W, H);
        if (m_source && m_zoom > 0) layout_children();
    }
}

int ImageViewer::handle(int event) {
    if (event == FL_MOUSEWHEEL && has_image()) {
        // Only while the pointer is over the picture. Wheel events travel
        // between groups in FLTK, so without this the log view and the
        // parameter fields would zoom the preview instead of scrolling
        // themselves. Returning 0 lets those widgets have their wheel back.
        if (!Fl::event_inside(m_scroll)) return 0;

        // Wheel up moves closer, wheel down further away, always around the
        // pointer - that is where the eye already is. Returning 1 keeps the
        // event away from the Fl_Scroll inside, which would otherwise treat it
        // as an instruction to scroll.
        const int px = Fl::event_x() - m_canvas->x();
        const int py = Fl::event_y() - m_canvas->y();
        const double k = 1.25;
        zoom_at(px, py, m_zoom * (Fl::event_dy() > 0 ? k : 1.0 / k));
        return 1;
    }
    return Fl_Group::handle(event);
}

// Zoom without letting the picture slide out from under the pointer: the same
// spot of the image stays at (px, py), given in canvas coordinates.
//
// The zoom ceiling is a memory guard, not a taste call: the scaled copy keeps
// its own pixels, so a full resolution picture at 16x would ask for gigabytes.
// Around 48 megapixels (about 150 MB) is plenty to inspect any detail.
double ImageViewer::max_zoom() const {
    if (!m_source) return kMaxZoom;
    const double pixels = (double)m_orig_w * m_orig_h;
    if (pixels <= 0.0) return kMaxZoom;
    const double cap = 48.0 * 1024.0 * 1024.0 / pixels;
    return std::min(kMaxZoom, cap < 1.0 ? 1.0 : std::sqrt(cap));
}

void ImageViewer::zoom_at(int px, int py, double new_zoom) {
    if (!m_source || !m_scaled) return;
    const double top = max_zoom();
    if (new_zoom < 0.05) new_zoom = 0.05;
    if (new_zoom > top) new_zoom = top;
    if (new_zoom == m_zoom) return;

    // Where the pointer sits inside the picture, as a fraction of its size.
    const double fx = (px - (m_canvas->w() - m_scaled->w()) / 2.0) / m_scaled->w();
    const double fy = (py - (m_canvas->h() - m_scaled->h()) / 2.0) / m_scaled->h();

    m_zoom = new_zoom;
    apply_zoom();       // rebuilds m_scaled and resizes the canvas

    // Put that same fraction of the picture back under the pointer. scroll_to()
    // does not clamp on its own, and while the picture still fits there is
    // nothing to scroll at all: without this the canvas gets shoved aside the
    // moment zooming starts and everything drifts towards the top left corner.
    const int nx = (int)((m_canvas->w() - m_scaled->w()) / 2.0 + fx * m_scaled->w());
    const int ny = (int)((m_canvas->h() - m_scaled->h()) / 2.0 + fy * m_scaled->h());
    scroll_clamped(m_scroll->xposition() + (nx - px),
                   m_scroll->yposition() + (ny - py));
}

void ImageViewer::zoom_in() {
    if (!m_source) return;
    m_zoom = std::min(m_zoom * 1.25, max_zoom());
    apply_zoom();
}

void ImageViewer::zoom_out() {
    if (!m_source) return;
    m_zoom = std::max(m_zoom / 1.25, 0.05);
    apply_zoom();
}

void ImageViewer::zoom_fit() {
    if (!m_source) return;
    const int sb = scrollbar_px();
    int avail_w = std::max(1, m_scroll->w() - sb);
    int avail_h = std::max(1, m_scroll->h() - sb);
    double zx = (double)avail_w / std::max(1, m_orig_w);
    double zy = (double)avail_h / std::max(1, m_orig_h);
    m_zoom = std::min(zx, zy);
    if (m_zoom > 1.0) m_zoom = 1.0; // never upscale on "fit"
    if (m_zoom <= 0) m_zoom = 1.0;
    apply_zoom();
}

void ImageViewer::zoom_1to1() {
    if (!m_source) return;
    m_zoom = 1.0;
    apply_zoom();
}
