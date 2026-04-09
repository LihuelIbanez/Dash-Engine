#include "EditorApp.h"
#include <cstdio>
#include <string>

int main(int argc, char* argv[])
{
    EditorApp editor;
    std::string startupProjectPath;
    if (argc > 1 && argv[1]) startupProjectPath = argv[1];

    if (!editor.init(startupProjectPath)) {
        std::fprintf(stderr, "Failed to initialise the editor.\n");
        return 1;
    }
    editor.run();
    return 0;
}
