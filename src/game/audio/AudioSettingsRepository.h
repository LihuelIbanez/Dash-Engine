#pragma once

#include <string>

class SqliteDb;
class AudioEngine;

// ─────────────────────────────────────────────────────────────────────────────
// AudioSettingsRepository — persists audio preferences in the project_meta
// key-value table so volume levels survive restarts.
// ─────────────────────────────────────────────────────────────────────────────
class AudioSettingsRepository {
public:
    explicit AudioSettingsRepository(SqliteDb& db);

    void loadInto(AudioEngine& engine);
    void save(const AudioEngine& engine);

    void  setFloat(const std::string& key, float value);
    float getFloat(const std::string& key, float defaultVal) const;

private:
    SqliteDb& db_;
};
