// files.cpp: the files module: the tools on files, view, str_replace and
// create_file: the shell whose machine they act on, none for the machine of
// the server; the bytes of text view returns at most, its head and its tail;
// the bytes of a file a tool loads whole, an image view reads, a text
// str_replace rewrites or a listing through a shell; the megapixels view
// decodes at most, and the longest side and the JPEG quality of an image it
// makes again

#include "engine/engine.h"
#include "engine/text.h"

#include <algorithm>

// the shell names an item of shells
static std::string validate(const engine_reader & config) {
    const auto    shell = config.get("files shell");
    const words_t names = config.names("shells");
    if (shell && std::find(names.begin(), names.end(), *shell) == names.end()) {
        return "files shell " + *shell + " names no item of shells";
    }
    return "";
}

static engine_module make_files() {
    // where the tools on files act is the administrator's, as where the
    // shells run is
    engine_key shell("shell", ENGINE_STRING);
    shell.admin = true;

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
    m.name     = "files";
    m.validate = validate;
    m.keys.push_back(shell);
    m.keys.push_back(output);
    m.keys.push_back(load);
    m.keys.push_back(megapixels);
    m.keys.push_back(dimension);
    m.keys.push_back(quality);
    return m;
}

ENGINE_MODULE(make_files);
