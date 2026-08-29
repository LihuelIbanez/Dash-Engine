#pragma once

#include <string>

#include <nlohmann/json.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// MaterialAsset — serialisable surface description referenced by
// RenderComponent::material. Kept deliberately small: every field here is
// consumed by the renderer.
// ─────────────────────────────────────────────────────────────────────────────
struct MaterialAsset {
    std::string guid;
    std::string name = "default";
    std::string albedoTexture;              // path; empty = flat white
    float baseColor[3] = {1.0f, 1.0f, 1.0f}; // multiplied with the instance color
    // Metallic/roughness workflow, fed to assets/shaders/pbr.glsl.
    float metallic = 0.0f;
    float roughness = 0.8f;

    nlohmann::json toJson() const;
    static MaterialAsset fromJson(const nlohmann::json& j);

    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);
};
