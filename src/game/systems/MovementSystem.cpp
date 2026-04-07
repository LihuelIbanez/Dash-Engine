#include "MovementSystem.h"
#include <SDL2/SDL.h>

void MovementSystem::update(RuntimeContext& ctx)
{
    if (!ctx.player->isAlive()) { ctx.running = false; return; }
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    ctx.player->handleInput(keys, ctx.dt);
    ctx.player->update(ctx.dt);
}
