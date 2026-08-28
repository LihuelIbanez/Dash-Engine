#pragma once
#include <algorithm>
#include <cstdint>

namespace dash::playmode {

// ─────────────────────────────────────────────────────────────────────────────
// PlaybackController – editor-side transport for Play mode. Mirrors the
// runtime's `PlaybackControl` block (see rendering/vulkan/EditorBridge.h):
// `stepSerial` is a monotonic counter, so the runtime advances exactly one
// frame per new value and a step is never lost nor replayed.
// ─────────────────────────────────────────────────────────────────────────────
class PlaybackController {
public:
    static constexpr float kDefaultTimeScale = 1.0f;

    bool     paused()     const { return paused_; }
    float    timeScale()  const { return timeScale_; }
    uint32_t stepSerial() const { return stepSerial_; }

    void setPaused(bool paused)
    {
        if (paused_ == paused) return;
        paused_ = paused;
        dirty_  = true;
    }

    void togglePause() { setPaused(!paused_); }

    // Negative scales would run the simulation backwards; clamp them away.
    void setTimeScale(float scale)
    {
        const float clamped = std::max(0.0f, scale);
        if (timeScale_ == clamped) return;
        timeScale_ = clamped;
        dirty_     = true;
    }

    // Asks the runtime for exactly one frame. Only meaningful while paused.
    void requestStep()
    {
        ++stepSerial_;
        dirty_ = true;
    }

    void reset()
    {
        paused_     = false;
        timeScale_  = kDefaultTimeScale;
        stepSerial_ = 0;
        dirty_      = true;
    }

    // True once after any change, so callers only push the transport when needed.
    bool consumeDirty()
    {
        const bool wasDirty = dirty_;
        dirty_ = false;
        return wasDirty;
    }

private:
    bool     paused_     = false;
    float    timeScale_  = kDefaultTimeScale;
    uint32_t stepSerial_ = 0;
    bool     dirty_      = false;
};

} // namespace dash::playmode
