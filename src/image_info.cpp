// image_info.cpp - header-only facts about an image file
#include "image_info.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include <sys/stat.h>

bool image_dimensions(const std::string &path, int &w, int &h) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;

    unsigned char b[32] = {0};
    const size_t n = fread(b, 1, sizeof(b), f);

    auto be32 = [](const unsigned char *p) {
        return (int)(((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
                     ((unsigned)p[2] << 8) | (unsigned)p[3]);
    };
    auto le32 = [](const unsigned char *p) {
        return (int)((unsigned)p[0] | ((unsigned)p[1] << 8) |
                     ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24));
    };
    auto le16 = [](const unsigned char *p) {
        return (int)((unsigned)p[0] | ((unsigned)p[1] << 8));
    };

    bool ok = false;

    // PNG: 8-byte signature, then the IHDR chunk carries width and height.
    if (n >= 24 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') {
        w = be32(b + 16);
        h = be32(b + 20);
        ok = w > 0 && h > 0;
    }
    // GIF: little-endian size right after the signature.
    else if (n >= 10 && b[0] == 'G' && b[1] == 'I' && b[2] == 'F') {
        w = le16(b + 6);
        h = le16(b + 8);
        ok = w > 0 && h > 0;
    }
    // BMP: size at offset 18 (a negative height means a top-down bitmap).
    else if (n >= 26 && b[0] == 'B' && b[1] == 'M') {
        w = le32(b + 18);
        h = le32(b + 22);
        if (h < 0) h = -h;
        ok = w > 0 && h > 0;
    }
    // JPEG: walk the segment chain to the frame header, which has the size.
    else if (n >= 4 && b[0] == 0xFF && b[1] == 0xD8) {
        fseek(f, 2, SEEK_SET);
        for (int guard = 0; guard < 128; ++guard) {
            int c = fgetc(f);
            if (c == EOF) break;
            if (c != 0xFF) continue;
            int marker = fgetc(f);
            while (marker == 0xFF) marker = fgetc(f);   // fill bytes
            if (marker == EOF) break;
            if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD9)) continue;
            const int hi = fgetc(f), lo = fgetc(f);
            if (hi == EOF || lo == EOF) break;
            const int len = (hi << 8) | lo;
            if (len < 2) break;
            // SOF0..SOF15 except DHT (C4), JPG (C8) and DAC (CC).
            if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 &&
                marker != 0xC8 && marker != 0xCC) {
                fgetc(f);                                // sample precision
                h = (fgetc(f) << 8) | fgetc(f);
                w = (fgetc(f) << 8) | fgetc(f);
                ok = w > 0 && h > 0;
                break;
            }
            fseek(f, len - 2, SEEK_CUR);
        }
    }

    fclose(f);
    return ok;
}

std::string image_format_name(const std::string &path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && dot < slash) return "";
    std::string ext = path.substr(dot + 1);
    for (char &c : ext) c = (char)std::toupper((unsigned char)c);
    if (ext == "JPG") ext = "JPEG";
    return ext;
}

std::string human_size(long long bytes) {
    char b[64];
    if (bytes < 1024)
        std::snprintf(b, sizeof(b), "%lld B", bytes);
    else if (bytes < 1024LL * 1024)
        std::snprintf(b, sizeof(b), "%.0f KB", (double)bytes / 1024.0);
    else
        std::snprintf(b, sizeof(b), "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    return b;
}

ImageInfo image_info(const std::string &path) {
    ImageInfo info;
    info.format = image_format_name(path);

    struct stat st;
    if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode))
        info.bytes = (long long)st.st_size;

    image_dimensions(path, info.width, info.height);
    return info;
}

std::string image_info_text(const std::string &path) {
    const ImageInfo info = image_info(path);
    if (!info.valid()) return "";
    char b[256];
    std::snprintf(b, sizeof(b), "%d x %d  \xC2\xB7  %s  \xC2\xB7  %s",
                  info.width, info.height,
                  info.format.empty() ? "?" : info.format.c_str(),
                  human_size(info.bytes).c_str());
    return b;
}
