#pragma once

#include <string>

#include "AudioMath.h"
#include "ISystem.h"

class AssetDatabase;
class AudioEngine;

namespace dash::audio {

// 8-4-4-4-12 hex, the shape AssetDatabase::generateGuid() produces. Same test
// Renderer::resolveSceneMaterials() applies to material references.
bool looksLikeGuid(const std::string& s);

// A GUID clip resolves to its asset sourcePath; every other reference — and any
// GUID the database does not know — comes back untouched and is used as a path.
std::string resolveClipPath(const std::string& clip, const AssetDatabase* assets);

// The game world is an x/y ground plane plus z height, while the audio maths use
// a Y-up right-handed space. Mirroring y keeps the stereo image aligned with the
// isometric screen axes.
inline Vec3 worldToAudio(float wx, float wy, float wz) { return Vec3{wx, wz, -wy}; }

// Ground-projected forward of the isometric camera, which faces world +x/+y.
inline Vec3 isometricCameraForward() { return Vec3{0.70710678f, 0.f, -0.70710678f}; }

}  // namespace dash::audio

// ─────────────────────────────────────────────────────────────────────────────
// AudioSystem — drives the AudioComponents of the scene: fires playOnStart
// emitters once, follows the entity they are attached to, stops the ones whose
// entity died or that got disabled, and pumps AudioEngine once per frame.
// ─────────────────────────────────────────────────────────────────────────────
class AudioSystem : public ISystem {
public:
    AudioSystem(AudioEngine* engine, const AssetDatabase* assets, std::string assetsRoot);

    void update(RuntimeContext& ctx) override;
    const char* name() const override { return "Audio"; }

private:
    std::string clipFilePath(const std::string& clip) const;

    AudioEngine*         engine_ = nullptr;
    const AssetDatabase* assets_ = nullptr;
    std::string          assetsRoot_;
};
