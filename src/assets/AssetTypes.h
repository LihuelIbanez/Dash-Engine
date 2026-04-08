#pragma once

enum class AssetType {
    Texture,
    TileSet,
    Scene,
    GameplayConfig,
    Prefab,
    Unknown
};

inline const char* assetTypeToStr(AssetType t) {
    switch (t) {
        case AssetType::Texture:        return "Texture";
        case AssetType::TileSet:        return "TileSet";
        case AssetType::Scene:          return "Scene";
        case AssetType::GameplayConfig: return "GameplayConfig";
        case AssetType::Prefab:         return "Prefab";
        default:                        return "Unknown";
    }
}
