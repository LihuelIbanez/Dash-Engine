#include "Game.h"
#include <cstdio>

int main(int argc, char* argv[])
{
    Game game;
    if (argc > 1) game.setSceneFile(argv[1]);
    if (!game.init()) {
        std::fprintf(stderr, "Failed to initialise the game.\n");
        return 1;
    }
    game.run();
    return 0;
}
