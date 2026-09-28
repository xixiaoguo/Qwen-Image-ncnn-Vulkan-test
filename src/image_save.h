// image_save.h - reading pictures, and writing scaled copies as PNG
#pragma once

#include <FL/Fl_Image.H>
#include <FL/Fl_RGB_Image.H>

#include <string>

// Decode a picture into an Fl_RGB_Image that owns its pixels, or nullptr.
//
// PNG goes through libpng, WebP through libwebp (when the build found it), and
// JPEG / BMP / GIF through FLTK's concrete decoders. Nothing here touches
// FLTK's image registry, so it is safe to call from a worker thread and does
// not depend on fl_register_images() having run. Alpha survives as a fourth
// channel when the file has one.
Fl_RGB_Image *load_image(const std::string &path);

// Decode `src`, scale it down to fit inside max_w x max_h keeping the aspect
// ratio (never enlarging), and write it to `dst`. When `align` is non-zero the
// result is rounded down to a multiple of it - the generator insists on
// multiples of 16, and asking it nicely is cheaper than reading its error.
// The size actually written comes back through out_w/out_h. Returns false when
// anything fails, including a build without libpng.
bool write_scaled_png(const std::string &src, const std::string &dst,
                      int max_w, int max_h, int align, int &out_w, int &out_h);

// Scale `src` to exactly w x h, ignoring the aspect ratio. Used for the inpaint
// mask, which the generator requires to be the same size as the picture beside
// it, pixel for pixel.
bool write_scaled_png_exact(const std::string &src, const std::string &dst,
                            int w, int h);
