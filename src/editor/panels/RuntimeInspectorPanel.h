#pragma once
#include "Reflection.h"
#include "SceneData.h"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// RuntimeInspectorPanel — live view of every entity/component while play-testing
//
// The pure logic (diff against the play snapshot, text filter and the sparkline
// ring buffer) lives in dash::editor::runtimeinspect and is header-only so the
// tests can exercise it without linking ImGui.
// ─────────────────────────────────────────────────────────────────────────────
namespace dash::editor::runtimeinspect {

inline constexpr float       kDefaultFloatEpsilon = 1e-5f;
inline constexpr std::size_t kDefaultHistorySize  = 120;

// ─────────────────────────────────────────────────────────────────────────────
// FieldKey — identity of one reflected field inside the scene
// ─────────────────────────────────────────────────────────────────────────────
struct FieldKey {
    uint64_t      entityId  = 0;
    ComponentType component = ComponentType::Transform;
    std::string   field;

    bool operator==(const FieldKey& o) const
    {
        return entityId == o.entityId && component == o.component && field == o.field;
    }
};

struct FieldKeyHash {
    std::size_t operator()(const FieldKey& k) const
    {
        const std::size_t golden = static_cast<std::size_t>(0x9e3779b97f4a7c15ULL);
        std::size_t h = std::hash<uint64_t>{}(k.entityId);
        h ^= std::hash<int>{}(static_cast<int>(k.component)) + golden + (h << 6) + (h >> 2);
        h ^= std::hash<std::string>{}(k.field) + golden + (h << 6) + (h >> 2);
        return h;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Value helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Read a reflected field out of a component without copying the whole struct.
inline PropertyValue readProperty(const ComponentVariant& comp, const PropertyInfo& prop)
{
    // Reflection only exposes a mutable fieldPtr; reading through it stays const-safe.
    return readFieldValue(fieldPtr(const_cast<ComponentVariant&>(comp), prop), prop.type);
}

/// Floats compare with an epsilon so numeric jitter is not reported as a change.
inline bool valuesEqual(const PropertyValue& a, const PropertyValue& b,
                        float eps = kDefaultFloatEpsilon)
{
    if (a.index() != b.index()) return false;
    if (const float* fa = std::get_if<float>(&a))
        return std::fabs(*fa - std::get<float>(b)) <= eps;
    return a == b;
}

inline std::string valueToString(const PropertyValue& v)
{
    if (const bool* b = std::get_if<bool>(&v))  return *b ? "true" : "false";
    if (const int* i = std::get_if<int>(&v))    return std::to_string(*i);
    if (const float* f = std::get_if<float>(&v)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.4g", static_cast<double>(*f));
        return std::string(buf);
    }
    return std::get<std::string>(v);
}

/// Strings have no meaningful plot; everything else feeds the sparkline.
inline bool asPlottable(const PropertyValue& v, float& out)
{
    if (const bool* b = std::get_if<bool>(&v))   { out = *b ? 1.f : 0.f; return true; }
    if (const int* i = std::get_if<int>(&v))     { out = static_cast<float>(*i); return true; }
    if (const float* f = std::get_if<float>(&v)) { out = *f; return true; }
    return false;
}

inline const ComponentVariant* findComponentOfType(const EntityData& e, ComponentType type)
{
    for (const auto& c : e.components)
        if (getVariantType(c) == type) return &c;
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Text filter
// ─────────────────────────────────────────────────────────────────────────────

inline bool containsCaseInsensitive(const std::string& haystack, const std::string& needle)
{
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    auto lower = [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        std::size_t j = 0;
        while (j < needle.size() && lower(haystack[i + j]) == lower(needle[j])) ++j;
        if (j == needle.size()) return true;
    }
    return false;
}

/// An entity passes when its own name or any of its component names matches.
inline bool entityMatchesFilter(const EntityData& e, const std::string& filter)
{
    if (filter.empty()) return true;
    if (containsCaseInsensitive(e.name, filter)) return true;
    for (const auto& c : e.components)
        if (containsCaseInsensitive(getComponentMeta(getVariantType(c)).name, filter))
            return true;
    return false;
}

/// A name hit on the entity keeps all of its components visible.
inline bool componentMatchesFilter(const EntityData& e, const ComponentVariant& comp,
                                   const std::string& filter)
{
    if (filter.empty()) return true;
    if (containsCaseInsensitive(e.name, filter)) return true;
    return containsCaseInsensitive(getComponentMeta(getVariantType(comp)).name, filter);
}

// ─────────────────────────────────────────────────────────────────────────────
// Diff against the snapshot taken when Play started
// ─────────────────────────────────────────────────────────────────────────────

struct FieldChange {
    FieldKey      key;
    PropertyValue before;
    PropertyValue after;
};

struct SceneDiff {
    std::vector<FieldChange> changedFields;
    std::vector<uint64_t>    addedEntities;
    std::vector<uint64_t>    removedEntities;

    std::unordered_set<FieldKey, FieldKeyHash> changedIndex;
    std::unordered_set<uint64_t>               changedEntities;

    bool fieldChanged(uint64_t entityId, ComponentType type, const std::string& field) const
    {
        FieldKey k;
        k.entityId  = entityId;
        k.component = type;
        k.field     = field;
        return changedIndex.count(k) > 0;
    }

    bool entityChanged(uint64_t entityId) const { return changedEntities.count(entityId) > 0; }

    bool entityAdded(uint64_t entityId) const
    {
        for (uint64_t id : addedEntities) if (id == entityId) return true;
        return false;
    }

    bool entityRemoved(uint64_t entityId) const
    {
        for (uint64_t id : removedEntities) if (id == entityId) return true;
        return false;
    }

    bool empty() const
    {
        return changedFields.empty() && addedEntities.empty() && removedEntities.empty();
    }

    void clear()
    {
        changedFields.clear();
        addedEntities.clear();
        removedEntities.clear();
        changedIndex.clear();
        changedEntities.clear();
    }
};

/// Compare the live scene against a baseline snapshot, field by field.
/// Entities present on only one side are reported as added/removed instead of
/// being diffed, so a spawn or a despawn during play never breaks the panel.
inline SceneDiff diffScenes(const SceneData& baseline, const SceneData& live,
                            float eps = kDefaultFloatEpsilon)
{
    SceneDiff diff;

    std::unordered_map<uint64_t, const EntityData*> baseById;
    baseById.reserve(baseline.entities.size());
    for (const auto& e : baseline.entities) baseById.emplace(e.id, &e);

    std::unordered_set<uint64_t> liveIds;
    liveIds.reserve(live.entities.size());

    for (const auto& live_e : live.entities) {
        liveIds.insert(live_e.id);

        auto it = baseById.find(live_e.id);
        if (it == baseById.end()) {
            diff.addedEntities.push_back(live_e.id);
            continue;
        }
        const EntityData& base_e = *it->second;

        for (const auto& comp : live_e.components) {
            const ComponentType type = getVariantType(comp);
            const ComponentVariant* baseComp = findComponentOfType(base_e, type);
            if (!baseComp) continue; // component added during play: nothing to compare against

            for (const auto& prop : getComponentMeta(type).properties) {
                PropertyValue after  = readProperty(comp, prop);
                PropertyValue before = readProperty(*baseComp, prop);
                if (valuesEqual(before, after, eps)) continue;

                FieldKey key;
                key.entityId  = live_e.id;
                key.component = type;
                key.field     = prop.name;

                FieldChange change;
                change.key    = key;
                change.before = before;
                change.after  = after;

                diff.changedIndex.insert(key);
                diff.changedEntities.insert(live_e.id);
                diff.changedFields.push_back(std::move(change));
            }
        }
    }

    for (const auto& base_e : baseline.entities)
        if (liveIds.count(base_e.id) == 0) diff.removedEntities.push_back(base_e.id);

    return diff;
}

// ─────────────────────────────────────────────────────────────────────────────
// ValueHistory — fixed-size ring buffer feeding ImGui::PlotLines
// ─────────────────────────────────────────────────────────────────────────────
class ValueHistory {
public:
    explicit ValueHistory(std::size_t capacity = kDefaultHistorySize)
        : buffer_(capacity == 0 ? 1 : capacity, 0.f) {}

    void push(float v)
    {
        buffer_[head_] = v;
        head_ = (head_ + 1) % buffer_.size();
        if (count_ < buffer_.size()) ++count_;
    }

    /// index 0 is the oldest retained sample.
    float at(std::size_t index) const
    {
        if (index >= count_) return 0.f;
        const std::size_t oldest = (head_ + buffer_.size() - count_) % buffer_.size();
        return buffer_[(oldest + index) % buffer_.size()];
    }

    std::vector<float> ordered() const
    {
        std::vector<float> out;
        out.reserve(count_);
        for (std::size_t i = 0; i < count_; ++i) out.push_back(at(i));
        return out;
    }

    float latest() const { return count_ == 0 ? 0.f : at(count_ - 1); }

    void minMax(float& lo, float& hi) const
    {
        lo = 0.f;
        hi = 0.f;
        if (count_ == 0) return;
        lo = hi = at(0);
        for (std::size_t i = 1; i < count_; ++i) {
            const float v = at(i);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
    }

    std::size_t size() const     { return count_; }
    std::size_t capacity() const { return buffer_.size(); }
    bool        empty() const    { return count_ == 0; }

    void clear() { head_ = 0; count_ = 0; }

private:
    std::vector<float> buffer_;
    std::size_t        head_  = 0;
    std::size_t        count_ = 0;
};

} // namespace dash::editor::runtimeinspect

// ─────────────────────────────────────────────────────────────────────────────
// RuntimeInspectorPanel — the ImGui window itself
//
// Autonomous: draw() receives everything it needs. `playSnapshot` may be null,
// in which case the panel diffs against a baseline it captures itself when Play
// starts (or on demand from the toolbar button).
// ─────────────────────────────────────────────────────────────────────────────
class RuntimeInspectorPanel {
public:
    using LogCallback = std::function<void(const std::string&)>;
    using FieldKey    = dash::editor::runtimeinspect::FieldKey;

    void draw(const SceneData& scene,
              bool isPlaying,
              uint64_t& selectedEntityId,
              const SceneData* playSnapshot = nullptr,
              LogCallback logCb = nullptr);

    void captureBaseline(const SceneData& scene);
    void clearBaseline();
    void clearWatches();

    bool isWatched(const FieldKey& key) const;
    void toggleWatch(const FieldKey& key);

    const dash::editor::runtimeinspect::SceneDiff& lastDiff() const { return diff_; }

private:
    void sampleWatches(const SceneData& scene, float dt);
    void drawToolbar(const SceneData& scene, bool isPlaying, const SceneData* baseline,
                     LogCallback& logCb);
    void drawWatches(const SceneData& scene, uint64_t& selectedEntityId);
    void drawEntityNode(const SceneData& scene, uint64_t entityId,
                        const std::unordered_set<uint64_t>& visible,
                        uint64_t& selectedEntityId, int depth);
    void drawComponentFields(const EntityData& entity, const ComponentVariant& comp);
    void rebuildHistories(std::size_t capacity);

    char        filterBuf_[128] = {0};
    std::string filter_;
    bool        showChangedOnly_ = false;
    bool        recording_       = true;
    bool        wasPlaying_      = false;
    bool        haveBaseline_    = false;
    SceneData   baseline_;

    dash::editor::runtimeinspect::SceneDiff diff_;

    std::vector<FieldKey> watches_;
    std::unordered_map<FieldKey,
                       dash::editor::runtimeinspect::ValueHistory,
                       dash::editor::runtimeinspect::FieldKeyHash> history_;

    float       sampleAccum_    = 0.f;
    float       sampleInterval_ = 0.05f;
    std::size_t historySize_    = dash::editor::runtimeinspect::kDefaultHistorySize;
};
