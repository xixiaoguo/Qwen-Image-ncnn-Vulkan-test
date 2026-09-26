// image_info.h - what can be learned about a picture without decoding it
#pragma once

#include <string>

// Width/height come from the file header, the format from the extension and the
// size from stat() - none of that needs the pixels, so it stays instant even
// for a large picture.
struct ImageInfo {
    int         width = 0;
    int         height = 0;
    std::string format;      // "PNG", "JPEG", "WEBP", ...
    long long   bytes = 0;
    bool valid() const { return width > 0 && height > 0; }
};

ImageInfo image_info(const std::string &path);

// "1024 x 1024 · PNG · 1.2 MB"; empty when the size could not be read.
std::string image_info_text(const std::string &path);

// "1.2 MB", "812 KB", "640 B"
std::string human_size(long long bytes);

// "PNG" for "a.png" (upper case; empty when there is no extension).
std::string image_format_name(const std::string &path);

// Width and height straight out of the header.
bool image_dimensions(const std::string &path, int &w, int &h);
