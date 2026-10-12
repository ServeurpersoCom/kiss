// view.cpp: the view module: the bytes of text the view tool returns at most,
// its head and its tail; the bytes of an image file it reads at most, the
// pixels it decodes at most, and the longest side and the JPEG quality of an
// image it makes again

#include "engine/engine.h"

static engine_module make_view() {
    engine_key output("output", ENGINE_NUMBER);
    output.min     = 2;
    output.integer = true;
    output.reset   = "65536";

    engine_key image("image", ENGINE_NUMBER);
    image.min     = 1;
    image.integer = true;
    image.reset   = "52428800";

    engine_key pixels("pixels", ENGINE_NUMBER);
    pixels.min     = 1;
    pixels.integer = true;
    pixels.reset   = "100000000";

    engine_key side("side", ENGINE_NUMBER);
    side.min     = 1;
    side.integer = true;
    side.reset   = "1920";

    engine_key quality("quality", ENGINE_NUMBER);
    quality.min     = 1;
    quality.max     = 100;
    quality.integer = true;
    quality.reset   = "85";

    engine_module m;
    m.name = "view";
    m.role =
        "the tool <hostname>_view: the bytes of a text, the bytes and pixels of an image, the side and quality it goes "
        "at";
    m.keys.push_back(output);
    m.keys.push_back(image);
    m.keys.push_back(pixels);
    m.keys.push_back(side);
    m.keys.push_back(quality);
    return m;
}

ENGINE_MODULE(make_view);
