// view.cpp: the view module: the bytes of text the view tool returns at most,
// its head and its tail, and the bytes of an image it returns at most

#include "engine/engine.h"

static engine_module make_view() {
    engine_key output("output", ENGINE_NUMBER);
    output.min     = 2;
    output.integer = true;

    engine_key image("image", ENGINE_NUMBER);
    image.min     = 1;
    image.integer = true;

    engine_module m;
    m.name = "view";
    m.keys.push_back(output);
    m.keys.push_back(image);
    return m;
}

ENGINE_MODULE(make_view);
