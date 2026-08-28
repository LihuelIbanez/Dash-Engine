#pragma once
#include "World.h"
#include "Player.h"
#include "Enemy.h"
#include <vector>
#include <memory>

class EventDispatcher;   // forward declaration (src/core/events/EventDispatcher.h)
struct AudioEmitter;     // forward declaration (src/game/audio/AudioEmitter.h)

// ─────────────────────────────────────────────────────────────────────────────
// RuntimeContext – shared state passed to every system each frame
// ─────────────────────────────────────────────────────────────────────────────
struct RuntimeContext {
    float  dt        = 0.f;       // delta time (seconds)
    bool   running   = true;      // set to false to end game loop

    World*                                world   = nullptr;
    Player*                               player  = nullptr;
    std::vector<std::unique_ptr<Enemy>>*  enemies = nullptr;

    int*   score = nullptr;       // points counter

    EventDispatcher* events = nullptr;   // event bus (owned by Game)

    std::vector<AudioEmitter>* audioEmitters = nullptr;   // scene sound sources (owned by Game)
};
