// image.cpp: an image as a model is sent it, with stb: decoded, resized in
// linear light, turned as its EXIF says, laid on white, encoded as JPEG, an
// opaque PNG that fits kept when it weighs less than its JPEG

#include "lib/image.h"

#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

std::string image_type(const std::string & head) {
    if (head.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0) {
        return "image/png";
    }
    if (head.compare(0, 3, "\xff\xd8\xff") == 0) {
        return "image/jpeg";
    }
    if (head.compare(0, 6, "GIF87a") == 0 || head.compare(0, 6, "GIF89a") == 0) {
        return "image/gif";
    }
    if (head.size() >= 12 && head.compare(0, 4, "RIFF") == 0 && head.compare(8, 4, "WEBP") == 0) {
        return "image/webp";
    }
    return "";
}

// a byte of the file, 0 past its end, so a header that lies reads no further
static unsigned at(const std::string & s, size_t i) {
    return i < s.size() ? (unsigned char) s[i] : 0;
}

// the size of a WebP from its first chunk: lossy, lossless or extended
static bool webp_size(const std::string & s, int & w, int & h) {
    if (s.compare(12, 4, "VP8 ") == 0) {
        w = (int) ((at(s, 26) | at(s, 27) << 8) & 0x3fff);
        h = (int) ((at(s, 28) | at(s, 29) << 8) & 0x3fff);
    } else if (s.compare(12, 4, "VP8L") == 0) {
        const unsigned bits = at(s, 21) | at(s, 22) << 8 | at(s, 23) << 16 | at(s, 24) << 24;
        w                   = (int) (bits & 0x3fff) + 1;
        h                   = (int) ((bits >> 14) & 0x3fff) + 1;
    } else if (s.compare(12, 4, "VP8X") == 0) {
        w = (int) (at(s, 24) | at(s, 25) << 8 | at(s, 26) << 16) + 1;
        h = (int) (at(s, 27) | at(s, 28) << 8 | at(s, 29) << 16) + 1;
    } else {
        return false;
    }
    return w > 0 && h > 0;
}

// the size and the EXIF orientation of a JPEG, read from its segments up to
// the first scan: the size from its frame header, the orientation, 1 to 8,
// from the tag 0x0112 of the first directory of an APP1 that opens on Exif,
// in the byte order its TIFF header names; 1 when it records none
static bool jpeg_header(const std::string & s, int & w, int & h, int & orientation) {
    orientation = 1;
    w = h = 0;
    for (size_t i = 2; at(s, i) == 0xff && i + 4 <= s.size();) {
        const unsigned marker = at(s, i + 1);
        const size_t   length = at(s, i + 2) << 8 | at(s, i + 3);
        if (marker == 0xda || marker == 0xd9 || length < 2) {
            break;
        }
        const bool frame = marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc;
        if (frame) {
            h = (int) (at(s, i + 5) << 8 | at(s, i + 6));
            w = (int) (at(s, i + 7) << 8 | at(s, i + 8));
        }
        if (marker == 0xe1 && s.compare(i + 4, 6, std::string("Exif\0\0", 6)) == 0) {
            const size_t tiff   = i + 10;
            const bool   little = at(s, tiff) == 'I';
            const auto   u16    = [&](size_t p) {
                return little ? at(s, p) | at(s, p + 1) << 8 : at(s, p) << 8 | at(s, p + 1);
            };
            const auto u32 = [&](size_t p) {
                return little ? u16(p) | (size_t) u16(p + 2) << 16 : (size_t) u16(p) << 16 | u16(p + 2);
            };
            const size_t dir = tiff + u32(tiff + 4);
            for (size_t k = 0, n = u16(dir); k < n; k++) {
                const size_t entry = dir + 2 + 12 * k;
                if (u16(entry) == 0x0112) {
                    const unsigned v = u16(entry + 8);
                    orientation      = v >= 1 && v <= 8 ? (int) v : 1;
                    break;
                }
            }
        }
        i += 2 + length;
    }
    return w > 0 && h > 0;
}

// the size an image declares in its header, read before anything decodes it,
// and its EXIF orientation, 1 but for a JPEG that records another
static bool declared_size(const std::string & s, const std::string & mime, int & w, int & h, int & orientation) {
    orientation = 1;
    if (mime == "image/png") {
        w = (int) (at(s, 16) << 24 | at(s, 17) << 16 | at(s, 18) << 8 | at(s, 19));
        h = (int) (at(s, 20) << 24 | at(s, 21) << 16 | at(s, 22) << 8 | at(s, 23));
        return w > 0 && h > 0;
    }
    if (mime == "image/gif") {
        w = (int) (at(s, 6) | at(s, 7) << 8);
        h = (int) (at(s, 8) | at(s, 9) << 8);
        return w > 0 && h > 0;
    }
    if (mime == "image/jpeg") {
        return jpeg_header(s, w, h, orientation);
    }
    return mime == "image/webp" && webp_size(s, w, h);
}

// the pixel of the source an upright pixel shows, as orientation turns or
// mirrors the source of w by h
static size_t source_of(int orientation, int x, int y, int w, int h) {
    int sx = x, sy = y;
    switch (orientation) {
        case 2:
            sx = w - 1 - x;
            break;
        case 3:
            sx = w - 1 - x;
            sy = h - 1 - y;
            break;
        case 4:
            sy = h - 1 - y;
            break;
        case 5:
            sx = y;
            sy = x;
            break;
        case 6:
            sx = y;
            sy = h - 1 - x;
            break;
        case 7:
            sx = w - 1 - y;
            sy = h - 1 - x;
            break;
        case 8:
            sx = w - 1 - y;
            sy = x;
            break;
        default:
            break;
    }
    return (size_t) sy * (size_t) w + (size_t) sx;
}

static void append(void * context, void * data, int size) {
    static_cast<std::string *>(context)->append(static_cast<const char *>(data), (size_t) size);
}

image_sent image_fit(const std::string & bytes, const image_limits & limits) {
    image_sent out;
    out.source_mime = image_type(bytes);
    int & w         = out.source_width;
    int & h         = out.source_height;

    int orientation = 1;
    if (!declared_size(bytes, out.source_mime, w, h, orientation)) {
        out.error = "Cannot read the size of the " + out.source_mime;
        return out;
    }
    if ((uint64_t) w * (uint64_t) h > limits.pixels) {
        out.error = "Image of " + std::to_string(w) + "x" + std::to_string(h) + ", more than " +
                    std::to_string(limits.pixels) + " pixels";
        return out;
    }
    const bool turned    = orientation >= 5;
    const int  upright_w = turned ? h : w;
    const int  upright_h = turned ? w : h;
    const bool fits      = std::max(w, h) <= limits.side;

    // a WebP goes as it is, view decoding none
    if (out.source_mime == "image/webp" && !fits) {
        out.error = "WebP of " + std::to_string(w) + "x" + std::to_string(h) + ", longer than " +
                    std::to_string(limits.side) + " px, and view resizes no WebP";
        return out;
    }
    if (out.source_mime == "image/webp" || (out.source_mime == "image/jpeg" && orientation == 1 && fits)) {
        out.data   = bytes;
        out.mime   = out.source_mime;
        out.width  = w;
        out.height = h;
        return out;
    }

    // the decoded pixels, RGBA, freed by stb
    int                                        channels = 0;
    std::unique_ptr<stbi_uc, void (*)(void *)> pixels(
        stbi_load_from_memory((const stbi_uc *) bytes.data(), (int) bytes.size(), &w, &h, &channels, 4),
        stbi_image_free);
    if (!pixels) {
        out.error = std::string("Cannot decode the image: ") + stbi_failure_reason();
        return out;
    }
    const size_t count = (size_t) w * (size_t) h;
    for (size_t i = 0; i < count && !out.transparent; i++) {
        out.transparent = pixels.get()[i * 4 + 3] < 255;
    }

    // the same scale on both sides, the source resized as it lies, turned after
    const double scale = std::min(1.0, (double) limits.side / std::max(upright_w, upright_h));
    out.width          = std::max(1, (int) std::lround(upright_w * scale));
    out.height         = std::max(1, (int) std::lround(upright_h * scale));
    const int rw       = turned ? out.height : out.width;
    const int rh       = turned ? out.width : out.height;

    std::vector<stbi_uc> resized((size_t) rw * (size_t) rh * 4);
    if (rw == w && rh == h) {
        std::copy(pixels.get(), pixels.get() + resized.size(), resized.begin());
    } else if (!stbir_resize_uint8_srgb(pixels.get(), w, h, 0, resized.data(), rw, rh, 0, STBIR_RGBA)) {
        out.error = "Cannot resize the image";
        return out;
    }
    pixels.reset();

    std::vector<stbi_uc> rgb((size_t) out.width * (size_t) out.height * 3);
    for (int y = 0; y < out.height; y++) {
        for (int x = 0; x < out.width; x++) {
            const stbi_uc * p = &resized[source_of(orientation, x, y, rw, rh) * 4];
            stbi_uc *       q = &rgb[((size_t) y * (size_t) out.width + (size_t) x) * 3];
            for (int c = 0; c < 3; c++) {
                q[c] = (stbi_uc) ((p[c] * p[3] + 255 * (255 - p[3]) + 127) / 255);
            }
        }
    }
    if (!stbi_write_jpg_to_func(append, &out.data, out.width, out.height, 3, rgb.data(), limits.quality)) {
        out.error = "Cannot encode the image";
        out.data.clear();
        return out;
    }
    // a PNG that needs no change goes as it is when its JPEG weighs more,
    // lossless and lighter, a drawing or a screenshot most of the time
    if (out.source_mime == "image/png" && orientation == 1 && fits && !out.transparent &&
        bytes.size() <= out.data.size()) {
        out.data = bytes;
        out.mime = out.source_mime;
        return out;
    }
    out.mime = "image/jpeg";
    out.made = true;
    return out;
}
