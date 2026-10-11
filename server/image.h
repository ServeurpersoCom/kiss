#pragma once
// image.h: an image as a model is sent it: known by its first bytes, a JPEG
// that fits goes as it is, any other comes back as a JPEG that fits, its
// proportions and its orientation kept, a transparent one laid on white

#include <cstddef>
#include <string>

struct image_limits {
    // the pixels an image may hold before it is decoded
    size_t pixels;
    // the longest side of the image sent, in pixels
    int    side;
    // the JPEG quality of an image made again, 1 to 100
    int    quality;
};

struct image_sent {
    std::string data;
    std::string mime;
    int         width  = 0;
    int         height = 0;
    // the image as read, before it was made again
    std::string source_mime;
    int         source_width  = 0;
    int         source_height = 0;
    bool        made          = false;
    bool        transparent   = false;
    // why no image is sent, empty when one is
    std::string error;
};

// the MIME type of an image by its first bytes, empty for anything else
std::string image_type(const std::string & head);

// the image the bytes of a file send, within the limits
image_sent image_fit(const std::string & bytes, const image_limits & limits);
