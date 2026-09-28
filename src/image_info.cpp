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

    // WebP: a RIFF container. The size sits in a VP8X (extended: alpha,
    // animation or metadata), VP8L (lossless) or VP8 (lossy) chunk. The chunk
    // list is walked rather than assuming the size chunk comes first, because a
    // metadata chunk in front of it - which some encoders write - must not hide
    // the picture's size.
    else if (n >= 12 && memcmp(b, "RIFF", 4) == 0 && memcmp(b + 8, "WEBP", 4) == 0) {
        fseek(f, 12, SEEK_SET);
        for (int guard = 0; guard < 16; ++guard) {
            unsigned char hdr[8];
            if (fread(hdr, 1, 8, f) != 8) break;
            const unsigned int csize = (unsigned)hdr[4] | ((unsigned)hdr[5] << 8) |
                                       ((unsigned)hdr[6] << 16) | ((unsigned)hdr[7] << 24);

            if (memcmp(hdr, "VP8X", 4) == 0) {
                // flags(1) + reserved(3), then width-1 and height-1 as 24-bit LE
                unsigned char d[10];
                if (fread(d, 1, 10, f) != 10) break;
                w = (d[4] | (d[5] << 8) | (d[6] << 16)) + 1;
                h = (d[7] | (d[8] << 8) | (d[9] << 16)) + 1;
                ok = w > 0 && h > 0;
                break;
            }
            if (memcmp(hdr, "VP8L", 4) == 0) {
                // signature byte 0x2f, then 14 bits width-1 and 14 bits height-1
                unsigned char d[5];
                if (fread(d, 1, 5, f) != 5 || d[0] != 0x2F) break;
                const unsigned int bits = (unsigned)d[1] | ((unsigned)d[2] << 8) |
                                          ((unsigned)d[3] << 16) | ((unsigned)d[4] << 24);
                w = (int)(bits & 0x3FFFu) + 1;
                h = (int)((bits >> 14) & 0x3FFFu) + 1;
                ok = w > 0 && h > 0;
                break;
            }
            if (memcmp(hdr, "VP8 ", 4) == 0) {
                // frame tag (3), the 0x9d012a start code (3), then 14-bit sizes
                unsigned char d[10];
                if (fread(d, 1, 10, f) != 10) break;
                if (d[3] != 0x9D || d[4] != 0x01 || d[5] != 0x2A) break;
                w = (d[6] | (d[7] << 8)) & 0x3FFF;
                h = (d[8] | (d[9] << 8)) & 0x3FFF;
                ok = w > 0 && h > 0;
                break;
            }

            // not it: hop to the next chunk (chunks are padded to even sizes)
            const unsigned int skip = csize + (csize & 1u);
            if (fseek(f, (long)skip, SEEK_CUR) != 0) break;
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
