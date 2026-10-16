// files.cpp: the files module: the tools on files, view, str_replace and
// create_file: the bytes of text view returns at most, its head and its
// tail; the bytes of a file a tool loads whole, an image view reads or a text
// str_replace rewrites; the megapixels view decodes at most, and the longest
// side and the JPEG quality of an image it makes again

#include "engine/engine.h"

static engine_module make_files() {
    engine_key output("max-output", ENGINE_NUMBER);
    output.min     = 2;
    output.integer = true;
    output.reset   = "65536";

    engine_key load("max-load", ENGINE_NUMBER);
    load.min     = 1;
    load.integer = true;
    load.reset   = "52428800";

    engine_key megapixels("max-megapixels", ENGINE_NUMBER);
    megapixels.min     = 1;
    megapixels.integer = true;
    megapixels.reset   = "100";

    engine_key dimension("max-dimension", ENGINE_NUMBER);
    dimension.min     = 1;
    dimension.integer = true;
    dimension.reset   = "1920";

    engine_key quality("jpeg-quality", ENGINE_NUMBER);
    quality.min     = 1;
    quality.max     = 100;
    quality.integer = true;
    quality.reset   = "85";

    engine_module m;
    m.name = "files";
    m.keys.push_back(output);
    m.keys.push_back(load);
    m.keys.push_back(megapixels);
    m.keys.push_back(dimension);
    m.keys.push_back(quality);
    return m;
}

ENGINE_MODULE(make_files);
