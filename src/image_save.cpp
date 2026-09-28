// image_save.cpp - reading pictures and writing scaled copies as PNG
#include "image_save.h"

#include <FL/Fl_BMP_Image.H>
#include <FL/Fl_GIF_Image.H>
#include <FL/Fl_JPEG_Image.H>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#ifdef HAVE_LIBPNG
#include <png.h>
#endif

#ifdef HAVE_LIBWEBP
#include <webp/decode.h>
#else
#include <dlfcn.h>      // libwebp is looked up at run time, see below
#endif

namespace {

std::string lower_ext(const std::string &path) {
    const size_t dot = path.rfind('.');
    const size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    std::string ext = path.substr(dot + 1);
    for (char &c : ext) c = (char)std::tolower((unsigned char)c);
    return ext;
}

#ifdef HAVE_LIBPNG
// Read a PNG into a picture that owns its bytes. Deliberately not via FLTK:
// Fl_PNG_Image chokes on the RGBA files the generators write, and the registry
// behind Fl_Shared_Image only answers after fl_register_images().
Fl_RGB_Image *read_png(const std::string &path) {
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) return nullptr;

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                             nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    if (!png || !info) {
        if (png) png_destroy_read_struct(&png, nullptr, nullptr);
        fclose(fp);
        return nullptr;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        return nullptr;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    const png_uint_32 w = png_get_image_width(png, info);
    const png_uint_32 h = png_get_image_height(png, info);
    const int color_type = png_get_color_type(png, info);
    const int bit_depth = png_get_bit_depth(png, info);

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    if (!(color_type & PNG_COLOR_MASK_ALPHA)) png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);

    png_read_update_info(png, info);

    std::vector<png_bytep> rows((size_t)h);
    std::vector<unsigned char> raw((size_t)w * 4 * h);
    for (png_uint_32 y = 0; y < h; ++y)
        rows[(size_t)y] = raw.data() + (size_t)y * w * 4;
    png_read_image(png, rows.data());
    png_read_end(png, nullptr);
    png_destroy_read_struct(&png, &info, nullptr);
    fclose(fp);

    // copy() gives a picture that owns its pixels; the vector is about to die.
    Fl_RGB_Image *tmp = new Fl_RGB_Image(raw.data(), (int)w, (int)h, 4);
    Fl_RGB_Image *owned = dynamic_cast<Fl_RGB_Image *>(tmp->copy((int)w, (int)h));
    delete tmp;
    return owned;
}

// Write 8-bit RGB or RGBA rows as a PNG.
bool write_png(const std::string &path, const unsigned char *px, int ld,
               int w, int h, int d) {
    FILE *fp = fopen(path.c_str(), "wb");
    if (!fp) return false;

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                              nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    if (!png || !info) {
        if (png) png_destroy_write_struct(&png, nullptr);
        fclose(fp);
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        remove(path.c_str());       // no half-written file left behind
        return false;
    }

    png_init_io(png, fp);
    png_set_IHDR(png, info, (png_uint_32)w, (png_uint_32)h, 8,
                 d == 4 ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    std::vector<png_bytep> rows((size_t)h);
    for (int y = 0; y < h; ++y)
        rows[(size_t)y] = const_cast<png_bytep>(px + (size_t)y * ld);
    png_write_image(png, rows.data());
    png_write_end(png, nullptr);

    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return true;
}
#endif  // HAVE_LIBPNG

// WebP decoding. A build that found libwebp-dev links it directly; otherwise
// the shared library is looked up on first use. That is not a trick for its own
// sake: the runtime package is on any desktop that can show a webp at all, and
// making someone install a -dev package just to look at a picture would be
// silly. When neither is available the file simply fails to decode and the
// caller reports it like any unreadable picture.

#ifndef HAVE_LIBWEBP
namespace {

// Resolved once. The dlopen handle is deliberately never closed - the function
// pointers have to stay valid for the life of the process.
struct WebPRuntime {
    unsigned char *(*decode_rgba)(const unsigned char *, size_t, int *, int *) = nullptr;
    void (*free_pixels)(void *) = nullptr;
    bool ok() const { return decode_rgba && free_pixels; }
};

const WebPRuntime &webp_runtime() {
    static const WebPRuntime rt = [] {
        WebPRuntime r;
        for (const char *soname : {"libwebp.so.7", "libwebp.so.6", "libwebp.so"}) {
            void *h = dlopen(soname, RTLD_LAZY | RTLD_LOCAL);
            if (!h) continue;
            r.decode_rgba = reinterpret_cast<unsigned char *(*)(const unsigned char *,
                                                                size_t, int *, int *)>(
                dlsym(h, "WebPDecodeRGBA"));
            r.free_pixels = reinterpret_cast<void (*)(void *)>(dlsym(h, "WebPFree"));
            if (r.ok()) break;
            dlclose(h);
            r = WebPRuntime{};
        }
        return r;
    }();
    return rt;
}

} // namespace
#endif  // !HAVE_LIBWEBP

// libwebp hands back a malloc'd RGBA buffer that has to be released with
// WebPFree - or with the identical function found via dlopen.
Fl_RGB_Image *read_webp(const std::string &path) {
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) return nullptr;
    fseek(fp, 0, SEEK_END);
    const long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (len <= 0) { fclose(fp); return nullptr; }
    std::vector<unsigned char> data((size_t)len);
    const size_t got = fread(data.data(), 1, (size_t)len, fp);
    fclose(fp);
    if (got != (size_t)len) return nullptr;

    int w = 0, h = 0;
    unsigned char *px = nullptr;
#ifndef HAVE_LIBWEBP
    const WebPRuntime &rt = webp_runtime();
    if (!rt.ok()) return nullptr;           // no libwebp anywhere: give up cleanly
#endif

#ifdef HAVE_LIBWEBP
    px = WebPDecodeRGBA(data.data(), data.size(), &w, &h);
#else
    px = rt.decode_rgba(data.data(), data.size(), &w, &h);
#endif
    if (!px || w <= 0 || h <= 0) {
#ifdef HAVE_LIBWEBP
        if (px) WebPFree(px);
#else
        if (px) rt.free_pixels(px);
#endif
        return nullptr;
    }

    // copy() gives a picture that owns its bytes, so libwebp's buffer can go.
    Fl_RGB_Image *tmp = new Fl_RGB_Image(px, w, h, 4);
    Fl_RGB_Image *owned = dynamic_cast<Fl_RGB_Image *>(tmp->copy(w, h));
    delete tmp;
#ifdef HAVE_LIBWEBP
    WebPFree(px);
#else
    rt.free_pixels(px);
#endif
    return owned;
}

} // namespace

Fl_RGB_Image *load_image(const std::string &path) {
    const std::string ext = lower_ext(path);

    if (ext == "png") {
#ifdef HAVE_LIBPNG
        return read_png(path);
#else
        return nullptr;         // built without libpng
#endif
    }
    if (ext == "webp") return read_webp(path);

    // FLTK's own decoders hand back an Fl_RGB_Image that owns its pixels, which
    // is exactly the shape wanted here. They do not need the registry, so this
    // stays safe to call off the UI thread.
    Fl_Image *img = nullptr;
    if (ext == "jpg" || ext == "jpeg") img = new Fl_JPEG_Image(path.c_str());
    else if (ext == "bmp")             img = new Fl_BMP_Image(path.c_str());
    else if (ext == "gif")             img = new Fl_GIF_Image(path.c_str());
    else return nullptr;        // not a format this program claims to show

    if (!img || img->w() <= 0 || img->h() <= 0) { if (img) img->release(); return nullptr; }
    Fl_RGB_Image *rgb = dynamic_cast<Fl_RGB_Image *>(img);
    if (!rgb) { img->release(); return nullptr; }
    return rgb;
}

// Shared tail: take a decoded picture, scale it to exactly w x h, write it.
bool write_at(Fl_Image *img, const std::string &dst, int w, int h, int &out_w, int &out_h) {
    Fl_Image *scaled = img->copy(w, h);
    img->release();
    if (!scaled) return false;

    Fl_RGB_Image *rgb = dynamic_cast<Fl_RGB_Image *>(scaled);
    bool ok = false;
    if (rgb && rgb->array && rgb->w() > 0 && rgb->h() > 0) {
        const int d = (rgb->d() == 4) ? 4 : 3;
        const int ld = rgb->ld() > 0 ? rgb->ld() : rgb->w() * d;
        ok = write_png(dst, rgb->array, ld, rgb->w(), rgb->h(), d);
        if (ok) { out_w = rgb->w(); out_h = rgb->h(); }
    }
    scaled->release();
    return ok;
}

bool write_scaled_png(const std::string &src, const std::string &dst,
                      int max_w, int max_h, int align, int &out_w, int &out_h) {
#ifdef HAVE_LIBPNG
    if (max_w < 1 || max_h < 1) return false;

    Fl_Image *img = load_image(src);
    if (!img) return false;

    const double k = std::min(1.0, std::min((double)max_w / img->w(),
                                            (double)max_h / img->h()));
    int w = std::max(1, (int)(img->w() * k + 0.5));
    int h = std::max(1, (int)(img->h() * k + 0.5));

    // Round down to the requested step: the generator refuses anything that is
    // not a multiple of 16, and being a few pixels smaller costs nothing.
    if (align > 1) {
        w = std::max(align, (w / align) * align);
        h = std::max(align, (h / align) * align);
    }
    return write_at(img, dst, w, h, out_w, out_h);
#else
    (void)src; (void)dst; (void)max_w; (void)max_h; (void)align;
    (void)out_w; (void)out_h;
    return false;       // no libpng: nothing here can write a PNG
#endif
}

bool write_scaled_png_exact(const std::string &src, const std::string &dst,
                            int w, int h) {
#ifdef HAVE_LIBPNG
    if (w < 1 || h < 1) return false;
    Fl_Image *img = load_image(src);
    if (!img) return false;
    int got_w = 0, got_h = 0;
    return write_at(img, dst, w, h, got_w, got_h);
#else
    (void)src; (void)dst; (void)w; (void)h;
    return false;
#endif
}
