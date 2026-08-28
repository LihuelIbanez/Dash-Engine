#pragma once

#include <string>

enum class AssetType {
    Texture,
    TileSet,
    Scene,
    GameplayConfig,
    Prefab,
    Sprite,
    Model,
    Material,
    Audio,
    Unknown
};

// Single source of truth for the persisted spelling of AssetType. The JSON and
// SQLite backends both go through here; keeping separate copies is what made
// Prefab/Sprite/Model round-trip as "Unknown".
inline const char* assetTypeToStr(AssetType t) {
    switch (t) {
        case AssetType::Texture:        return "Texture";
        case AssetType::TileSet:        return "TileSet";
        case AssetType::Scene:          return "Scene";
        case AssetType::GameplayConfig: return "GameplayConfig";
        case AssetType::Prefab:         return "Prefab";
        case AssetType::Sprite:         return "Sprite";
        case AssetType::Model:          return "Model";
        case AssetType::Material:       return "Material";
        case AssetType::Audio:          return "Audio";
        default:                        return "Unknown";
    }
}

inline AssetType assetTypeFromStr(const std::string& s) {
    if (s == "Texture")        return AssetType::Texture;
    if (s == "TileSet")        return AssetType::TileSet;
    if (s == "Scene")          return AssetType::Scene;
    if (s == "GameplayConfig") return AssetType::GameplayConfig;
    if (s == "Prefab")         return AssetType::Prefab;
    if (s == "Sprite")         return AssetType::Sprite;
    if (s == "Model")          return AssetType::Model;
    if (s == "Material")       return AssetType::Material;
    if (s == "Audio")          return AssetType::Audio;
    return AssetType::Unknown;
}
