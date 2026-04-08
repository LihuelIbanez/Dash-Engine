#pragma once
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// SpriteAsset — describes a sprite file tracked by AssetDatabase.
// The source PNG lives at assets/sprites/<name>.png.
// Metadata (anchor, pivot) is stored alongside as <name>.sprite.json.
// ─────────────────────────────────────────────────────────────────────────────
struct SpriteAsset {
    std::string guid;
    std::string sourcePath;   // relative: assets/sprites/<name>.png
    int         width  = 16;
    int         height = 16;
    std::string anchor = "BottomCenter";
    float       pivotX = 0.5f;
    float       pivotY = 1.0f;
};
