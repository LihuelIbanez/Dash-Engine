#pragma once

#include "AudioEngine.h"
#include "components/Components.h"

// ─────────────────────────────────────────────────────────────────────────────
// Bridges the scene-side AudioComponent onto AudioEngine::PlayParams so the
// engine itself stays free of any dependency on the component headers.
// ─────────────────────────────────────────────────────────────────────────────

inline AudioEngine::PlayParams audioPlayParamsFrom(const AudioComponent& c,
                                                   float x, float y, float z)
{
    AudioEngine::PlayParams p;
    p.volume      = c.volume;
    p.pitch       = c.pitch;
    p.loop        = c.loop;
    p.spatial     = c.spatial;
    p.x           = x;
    p.y           = y;
    p.z           = z;
    p.minDistance = c.minDistance;
    p.maxDistance = c.maxDistance;
    p.bus         = c.bus;
    return p;
}
