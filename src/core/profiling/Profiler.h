#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>

// ─────────────────────────────────────────────────────────────────────────────
// Profiler – lightweight per-subsystem timing + frame-time tracking
//
// Usage:
//   Profiler::instance().beginFrame();
//   { auto s = Profiler::instance().scope("AI");  ... }
//   { auto s = Profiler::instance().scope("Render"); ... }
//   Profiler::instance().endFrame();
// ─────────────────────────────────────────────────────────────────────────────

class Profiler {
public:
    static Profiler& instance();

    // ── Frame lifecycle ──────────────────────────────────────────────────────
    void beginFrame();
    void endFrame();

    // ── Scoped timer ─────────────────────────────────────────────────────────
    class ScopeTimer {
    public:
        ScopeTimer(Profiler& prof, const std::string& name);
        ~ScopeTimer();
        ScopeTimer(const ScopeTimer&) = delete;
        ScopeTimer& operator=(const ScopeTimer&) = delete;
    private:
        Profiler&   prof_;
        std::string name_;
        std::chrono::high_resolution_clock::time_point start_;
    };

    ScopeTimer scope(const std::string& name);

    // ── Query data ───────────────────────────────────────────────────────────
    struct SectionTiming {
        std::string name;
        float       lastMs  = 0.f;   // last frame
        float       avgMs   = 0.f;   // rolling average
        float       peakMs  = 0.f;   // all-time peak
    };

    float frameDtMs()   const { return frameDtMs_; }
    float frameAvgMs()  const { return frameAvgMs_; }
    float framePeakMs() const { return framePeakMs_; }
    float fps()         const { return frameDtMs_ > 0.f ? 1000.f / frameDtMs_ : 0.f; }

    const std::vector<SectionTiming>& sections() const { return sections_; }

    // Record a named section with explicit duration in seconds
    void record(const std::string& name, float seconds);

private:
    Profiler() = default;

    // Frame timing
    std::chrono::high_resolution_clock::time_point frameStart_;
    float frameDtMs_   = 0.f;
    float frameAvgMs_  = 0.f;
    float framePeakMs_ = 0.f;

    // Per-section data (ordered by first-seen)
    std::vector<SectionTiming>                   sections_;
    std::unordered_map<std::string, std::size_t> sectionIndex_;

    SectionTiming& getOrCreate(const std::string& name);

    static constexpr float kAlpha = 0.05f;   // EMA smoothing factor
};
