#pragma once
#include <cstdint>
#include <functional>
#include <string>

class AssetDatabase;
class CommandStack;
class World;
struct SceneData;

// ─────────────────────────────────────────────────────────────────────────────
// AudioPanel — audio assets, bus volumes and the AudioComponents in the scene.
//
// The mixer sliders are editor-side state only: nothing here talks to the
// AudioEngine yet, so preview stays disabled until playback is wired up.
// ─────────────────────────────────────────────────────────────────────────────
class AudioPanel {
public:
    using LogCallback = std::function<void(const std::string&)>;

    void draw(const AssetDatabase& db,
              SceneData& scene,
              World& world,
              CommandStack& commandStack,
              uint64_t& selectedEntityId,
              LogCallback logCb = nullptr);

    float masterVolume() const { return masterVolume_; }
    float sfxVolume()    const { return sfxVolume_; }
    float musicVolume()  const { return musicVolume_; }

    const std::string& selectedClipGuid() const { return selectedClipGuid_; }

private:
    float       masterVolume_ = 1.f;
    float       sfxVolume_    = 1.f;
    float       musicVolume_  = 1.f;
    std::string selectedClipGuid_;
};
