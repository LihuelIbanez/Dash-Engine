#include "Profiler.h"
#include <algorithm>
#include <cstdio>

using Clock = std::chrono::high_resolution_clock;

// ─────────────────────────────────────────────────────────────────────────────
// Singleton
// ─────────────────────────────────────────────────────────────────────────────
Profiler& Profiler::instance()
{
    static Profiler s;
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame lifecycle
// ─────────────────────────────────────────────────────────────────────────────
void Profiler::beginFrame()
{
    frameStart_ = Clock::now();
}

void Profiler::endFrame()
{
    auto now = Clock::now();
    float ms = std::chrono::duration<float, std::milli>(now - frameStart_).count();
    frameDtMs_ = ms;

    // Exponential moving average
    if (frameAvgMs_ <= 0.f)
        frameAvgMs_ = ms;
    else
        frameAvgMs_ = frameAvgMs_ * (1.f - kAlpha) + ms * kAlpha;

    if (ms > framePeakMs_) {
        framePeakMs_ = ms;
        if (ms > 33.3f) {  // > 30fps threshold
            std::printf("[Profiler] Frame spike: %.2f ms\n", ms);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Section management
// ─────────────────────────────────────────────────────────────────────────────
Profiler::SectionTiming& Profiler::getOrCreate(const std::string& name)
{
    auto it = sectionIndex_.find(name);
    if (it != sectionIndex_.end())
        return sections_[it->second];

    std::size_t idx = sections_.size();
    sectionIndex_[name] = idx;
    sections_.push_back({name, 0.f, 0.f, 0.f});
    return sections_.back();
}

void Profiler::record(const std::string& name, float seconds)
{
    float ms = seconds * 1000.f;
    auto& s  = getOrCreate(name);
    s.lastMs = ms;

    if (s.avgMs <= 0.f)
        s.avgMs = ms;
    else
        s.avgMs = s.avgMs * (1.f - kAlpha) + ms * kAlpha;

    if (ms > s.peakMs)
        s.peakMs = ms;
}

// ─────────────────────────────────────────────────────────────────────────────
// ScopeTimer
// ─────────────────────────────────────────────────────────────────────────────
Profiler::ScopeTimer::ScopeTimer(Profiler& prof, const std::string& name)
    : prof_(prof), name_(name), start_(Clock::now())
{
}

Profiler::ScopeTimer::~ScopeTimer()
{
    auto elapsed = std::chrono::duration<float>(Clock::now() - start_).count();
    prof_.record(name_, elapsed);
}

Profiler::ScopeTimer Profiler::scope(const std::string& name)
{
    return ScopeTimer(*this, name);
}
