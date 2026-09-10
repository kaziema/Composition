// One-shot generator: writes schemas/effects/*.json from the registry.
#include <cstdio>
#include <fstream>
#include "ruby/engine/EffectRegistry.h"
#include "ruby/io/SchemaIO.h"
int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: gen <dir>\n"); return 1; }
    for (const auto& def : ruby::engine::EffectRegistry::instance().all()) {
        const std::string path = std::string(argv[1]) + "/" + def.schema.id + ".json";
        std::ofstream f(path);
        f << ruby::io::schemaToJson(def.schema);
        std::printf("%s\n", path.c_str());
    }
    return 0;
}
