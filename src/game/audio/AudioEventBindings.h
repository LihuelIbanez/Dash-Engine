#pragma once

class EventDispatcher;
class AudioEngine;

// ─────────────────────────────────────────────────────────────────────────────
// AudioEventBindings — maps gameplay events to audio cues.
// Subscribes to EventDispatcher and plays procedural tones via AudioEngine.
// Includes per-event cooldown to prevent audio spam.
// ─────────────────────────────────────────────────────────────────────────────
class AudioEventBindings {
public:
    void bindDefaults(EventDispatcher& dispatcher, AudioEngine& engine);

    // Advance internal clock (call once per frame with delta time).
    void tick(float dt);

private:
    static constexpr float kCooldownSec = 0.08f;

    float lastDamageTime_  = -1.0f;
    float lastDeathTime_   = -1.0f;
    float lastLootTime_    = -1.0f;
    float lastLevelUpTime_ = -1.0f;
    float elapsed_         = 0.0f;
};
