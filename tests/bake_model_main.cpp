// bake_model_main.cpp — offline helper, not a test.
//
// Runs ModelImporter over a source model and writes the .dashmesh/.dashskel/
// .dashanim triplet the runtime loads. Kept out of the default build
// (EXCLUDE_FROM_ALL); used to bake the checked-in wolf artifacts:
//
//   cmake --build build --target dash_bake_model
//   ./build/tests/dash_bake_model assets/models/gltf/Wolf-Blender-2.82a.gltf \
//                                 assets/models/gltf/Wolf-Blender-2.82a.dashmesh

#include <cstdio>
#include <string>

#include "AssetTypes.h"
#include "importers/ModelImporter.h"

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: dash_bake_model <source-model> <output.dashmesh>\n");
        return 2;
    }

    AssetRecord record;
    ModelImporter importer;
    const ImportResult result = importer.import(argv[1], argv[2], record);

    if (!result.success) {
        for (const std::string& e : result.errors) std::fprintf(stderr, "error: %s\n", e.c_str());
        return 1;
    }

    std::printf("baked %s -> %s\n", argv[1], argv[2]);
    return 0;
}
