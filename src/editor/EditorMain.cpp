#include "EditorApp.h"
#include <cstdio>

int main(int /*argc*/, char* /*argv*/[])
{
    EditorApp editor;
    if (!editor.init()) {
        std::fprintf(stderr, "Failed to initialise the editor.\n");
        return 1;
    }
    editor.run();
    return 0;
}
