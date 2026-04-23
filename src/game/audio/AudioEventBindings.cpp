#include "AudioEventBindings.h"
#include "AudioEngine.h"
#include "EventDispatcher.h"
#include "GameEvents.h"

void AudioEventBindings::tick(float dt)
{
    elapsed_ += dt;
}

void AudioEventBindings::bindDefaults(EventDispatcher& dispatcher, AudioEngine& engine)
{
    // DamageEvent → 220 Hz thud (80 ms)
    dispatcher.subscribe<DamageEvent>([this, &engine](const DamageEvent& /*e*/) {
        if (elapsed_ - lastDamageTime_ < kCooldownSec) return;
        lastDamageTime_ = elapsed_;
        engine.playTone(220.0f, 80.0f, 0.25f);
    });

    // DeathEvent → 110 Hz low boom (200 ms)
    dispatcher.subscribe<DeathEvent>([this, &engine](const DeathEvent& /*e*/) {
        if (elapsed_ - lastDeathTime_ < kCooldownSec) return;
        lastDeathTime_ = elapsed_;
        engine.playTone(110.0f, 200.0f, 0.35f);
    });

    // LevelUpEvent → 880 Hz chime (300 ms)
    dispatcher.subscribe<LevelUpEvent>([this, &engine](const LevelUpEvent& /*e*/) {
        if (elapsed_ - lastLevelUpTime_ < kCooldownSec) return;
        lastLevelUpTime_ = elapsed_;
        engine.playTone(880.0f, 300.0f, 0.30f);
    });

    // LootDropEvent → 440 Hz ding (120 ms)
    dispatcher.subscribe<LootDropEvent>([this, &engine](const LootDropEvent& /*e*/) {
        if (elapsed_ - lastLootTime_ < kCooldownSec) return;
        lastLootTime_ = elapsed_;
        engine.playTone(440.0f, 120.0f, 0.25f);
    });
}
