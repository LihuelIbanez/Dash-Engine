#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "AudioEngine.h"
#include "components/Components.h"

// ─────────────────────────────────────────────────────────────────────────────
// AudioEmitter — a scene AudioComponent bound to a runtime entity.
// The scene loader fills the description, AudioSystem owns the playback state
// and keeps the position in sync with whatever entity the emitter follows.
// ─────────────────────────────────────────────────────────────────────────────
struct AudioEmitter {
    // What drives the emitter position each frame.
    enum class Attachment : int { Static = 0, Player = 1, Enemy = 2 };

    std::uint64_t  entityId = 0;
    std::string    name;
    AudioComponent component;

    Attachment attachment = Attachment::Static;
    int        enemyIndex = -1;       // index into RuntimeContext::enemies when attached to one

    float x = 0.f, y = 0.f, z = 0.f;  // world position: x/y ground plane, z height
    bool  enabled = true;             // false stops the voice and keeps the emitter silent

    // ── Playback state, owned by AudioSystem ────────────────────────────────
    AudioEngine::VoiceHandle voice = AudioEngine::kInvalidVoice;
    bool started   = false;           // playOnStart already evaluated
    int  playCount = 0;               // times the emitter fired, even if the clip failed to decode
};

// Reads the AudioComponent of one scene entity. Entities without one produce no
// emitter, which is how the audio system ends up ignoring them.
inline std::optional<AudioEmitter> audioEmitterFromEntityJson(const nlohmann::json& entityJson)
{
    if (!entityJson.is_object()) return std::nullopt;
    if (!entityJson.contains("components") || !entityJson["components"].is_array()) return std::nullopt;

    const nlohmann::json* audioJson     = nullptr;
    const nlohmann::json* transformJson = nullptr;
    for (const auto& c : entityJson["components"]) {
        if (!c.is_object()) continue;
        const std::string type = c.value("type", std::string{});
        if (type == "Audio" && !audioJson)              audioJson = &c;
        else if (type == "Transform" && !transformJson) transformJson = &c;
    }
    if (!audioJson) return std::nullopt;

    AudioEmitter e;
    e.entityId = entityJson.value("id", std::uint64_t{0});
    e.name     = entityJson.value("name", std::string{});
    e.x        = entityJson.value("x", 0.f);
    e.y        = entityJson.value("y", 0.f);
    e.z        = transformJson ? transformJson->value("z", 0.f) : 0.f;

    AudioComponent& c = e.component;
    c.clip        = audioJson->value("clip", std::string{});
    c.volume      = audioJson->value("volume", 1.f);
    c.pitch       = audioJson->value("pitch", 1.f);
    c.loop        = audioJson->value("loop", false);
    c.playOnStart = audioJson->value("playOnStart", true);
    c.spatial     = audioJson->value("spatial", true);
    c.minDistance = audioJson->value("minDistance", 1.f);
    c.maxDistance = audioJson->value("maxDistance", 20.f);
    c.bus         = audioJson->value("bus", static_cast<int>(AudioBus::Sfx));
    return e;
}
