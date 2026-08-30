#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// InputBindings3D — configurable key bindings and sensitivity for 3D camera.
// Replaces hardcoded GLFW key constants and magic numbers in Renderer.cpp.
// Uses raw int key codes so this header does not depend on GLFW.
// ─────────────────────────────────────────────────────────────────────────────
struct InputBindings3D {
    // GLFW key codes (raw ints to avoid header dependency)
    int keyForward   = 87;  // GLFW_KEY_W
    int keyBackward  = 83;  // GLFW_KEY_S
    int keyLeft      = 65;  // GLFW_KEY_A
    int keyRight     = 68;  // GLFW_KEY_D
    int keyAttack    = 32;  // GLFW_KEY_SPACE
    int mouseButtonLook = 1; // GLFW_MOUSE_BUTTON_RIGHT
    int mouseButtonAttack = 0; // GLFW_MOUSE_BUTTON_LEFT

    // Tunables
    float moveSpeed        = 2.4f;
    float mouseSensitivity = 0.10f;
    float pitchMin         = -89.0f;
    float pitchMax         =  89.0f;

    static InputBindings3D defaults() { return {}; }
};
