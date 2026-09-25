#include "image_viewer.h"

#include <FL/Fl.H>
#include <FL/Fl_Image.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_JPEG_Image.H>
#include <FL/Fl_BMP_Image.H>
#include <FL/Fl_GIF_Image.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Window.H>

#include <algorithm>
#include <cctype>
#include <vector>

#ifdef HAVE_LIBPNG
#include <png.h>
#endif

ImageViewer::ImageViewer(int X, int Y, int W, int H, const char *L)
    : Fl_Group(X, Y, W, H, L) {
    box(FL_DOWN_BOX);
    color(FL_WHITE);

    m_scroll = new Fl_Scroll(X, Y, W, H);
    m_scroll->box(FL_FLAT_BOX);
    m_scroll->type(Fl_Scroll::BOTH);
    m_scroll->color(FL_WHITE);   // canvas floor: keep it white, not the window grey
    // Track the global Fl::scrollbar_size() (0 = no local override) so the
    // preview's scrollbars are exactly as narrow as the log view's.
    m_scroll->scrollbar_size(0);

    m_canvas = new Fl_Box(X, Y, W, H);
    m_canvas->box(FL_NO_BOX);
    m_canvas->color(FL_WHITE);
    m_canvas->align(FL_ALIGN_INSIDE | FL_ALIGN_CENTER);
    m_canvas->label("(no image)");
    m_canvas->labelsize(20);
    m_canvas->labelcolor(FL_DARK3);

    end();
    resizable(m_scroll);
}

ImageViewer::~ImageViewer() {
    delete m_scaled;
    delete m_source;
}

static std::string lower_ext(const std::string &p) {
    size_t dot = p.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string e = p.substr(dot + 1);
    for (char &c : e) c = (char)tolower((unsigned char)c);
    return e;
}

#ifdef HAVE_LIBPNG
// FLTK's Fl_PNG_Image cannot read RGBA (transparent) PNGs produced by
// qwenimage-ncnn-vulkan properly in all versions, so decode with libpng and
// composite alpha over a checkerboard to make transparency visible.
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

    // composite over a light checkerboard so alpha is visible
    static const int tile = 12;
    unsigned char c1 = 0xFF, c2 = 0xE0;
    std::vector<unsigned char> out((size_t)w * 4 * h);
    for (png_uint_32 y = 0; y < h; ++y) {
        for (png_uint_32 x = 0; x < w; ++x) {
            const unsigned char *s = raw.data() + ((size_t)y * w + x) * 4;
            unsigned char *d = out.data() + ((size_t)y * w + x) * 4;
            unsigned char bg = (((x / tile) + (y / tile)) & 1) ? c1 : c2;
            unsigned int al = s[3];
            d[0] = (unsigned char)((s[0] * al + bg * (255 - al)) / 255);
            d[1] = (unsigned char)((s[1] * al + bg * (255 - al)) / 255);
            d[2] = (unsigned char)((s[2] * al + bg * (255 - al)) / 255);
            d[3] = 255;
        }
    }
    // Fl_RGB_Image copies data by default? No - it does not. Use copy().
    Fl_RGB_Image *tmp = new Fl_RGB_Image(out.data(), (int)w, (int)h, 4);
    Fl_RGB_Image *copy = (Fl_RGB_Image *)tmp->copy((int)w, (int)h);
    delete tmp;
    return copy;
}
#endif

bool ImageViewer::load(const std::string &path) {
    clear();
    m_path = path;

    Fl_Image *img = nullptr;
    std::string ext = lower_ext(path);

    if (ext == "png") {
#ifdef HAVE_LIBPNG
        std::string err;
        img = load_png_rgba(path, err);
        if (!img) m_error = err;
#endif
        if (!img) {
            img = new Fl_PNG_Image(path.c_str());
            if (img->w() <= 0 || img->h() <= 0 || img->data() == nullptr) {
                delete img; img = nullptr;
            }
        }
    } else if (ext == "jpg" || ext == "jpeg") {
        img = new Fl_JPEG_Image(path.c_str());
        if (img->w() <= 0 || img->h() <= 0 || img->data() == nullptr) { delete img; img = nullptr; }
    } else if (ext == "bmp") {
        img = new Fl_BMP_Image(path.c_str());
        if (img->w() <= 0 || img->h() <= 0 || img->data() == nullptr) { delete img; img = nullptr; }
    } else if (ext == "gif") {
        img = new Fl_GIF_Image(path.c_str());
        if (img->w() <= 0 || img->h() <= 0 || img->data() == nullptr) { delete img; img = nullptr; }
    } else {
#if FLTK_ABI_VERSION >= 10304 || defined(FL_IMAGE_VERSION)
        img = Fl_Image::read(path.c_str());
#endif
        if (!img) {
            // last resort: try png then jpeg
            img = new Fl_PNG_Image(path.c_str());
            if (img->w() <= 0 || img->h() <= 0 || img->data() == nullptr) { delete img; img = nullptr; }
        }
    }

    if (!img) {
        if (m_error.empty()) m_error = "unsupported or unreadable image: " + path;
        m_canvas->image(nullptr);
        m_canvas->label(("(failed to load)\n" + m_error).c_str());
        m_canvas->redraw();
        return false;
    }

    m_source = img;
    m_error.clear();
    zoom_fit();
    return true;
}

void ImageViewer::clear() {
    m_canvas->image(nullptr);
    delete m_scaled;
    m_scaled = nullptr;
    delete m_source;
    m_source = nullptr;
    m_zoom = 1.0;
    m_error.clear();
    m_path.clear();
    m_canvas->label("(no image)");
    layout_children();
    redraw();
}

void ImageViewer::apply_zoom() {
    if (!m_source) return;
    delete m_scaled;
    m_scaled = nullptr;

    int sw = m_source->w();
    int sh = m_source->h();
    int w = std::max(1, (int)(sw * m_zoom + 0.5));
    int h = std::max(1, (int)(sh * m_zoom + 0.5));
    m_scaled = (Fl_RGB_Image *)m_source->copy(w, h);

    m_canvas->image(m_scaled);
    m_canvas->label(nullptr);
    layout_children();
    m_scroll->redraw();
    redraw();
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

    // No scroll_to() here: when the canvas shrinks back to the viewport Fl_Scroll
    // already parks it at the origin itself, and its internal offset is only
    // brought in line on the next scrollbar event. Calling scroll_to(0, 0) now
    // would act on that stale offset and shove the canvas off by the old amount.
    m_canvas->resize(m_scroll->x(), m_scroll->y(), cw, ch);
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

void ImageViewer::zoom_in() {
    if (!m_source) return;
    m_zoom = std::min(m_zoom * 1.25, 16.0);
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
    double zx = (double)avail_w / m_source->w();
    double zy = (double)avail_h / m_source->h();
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
