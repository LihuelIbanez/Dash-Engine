#include "AudioSettingsRepository.h"
#include "AudioEngine.h"
#include "db/SqliteDb.h"
#include "db/SqliteStatement.h"
#include <sqlite3.h>
#include <cstdio>

AudioSettingsRepository::AudioSettingsRepository(SqliteDb& db)
    : db_(db)
{
}

void AudioSettingsRepository::setFloat(const std::string& key, float value)
{
    auto stmt = db_.prepare(
        "INSERT OR REPLACE INTO project_meta(key, value) VALUES(?1, ?2)");
    if (!stmt.isValid()) return;
    stmt.bindText(1, key);
    stmt.bindText(2, std::to_string(value));
    stmt.step();
}

float AudioSettingsRepository::getFloat(const std::string& key, float defaultVal) const
{
    auto stmt = db_.prepare("SELECT value FROM project_meta WHERE key = ?1");
    if (!stmt.isValid()) return defaultVal;
    stmt.bindText(1, key);
    if (stmt.step() != SQLITE_ROW) return defaultVal;

    std::string val = stmt.columnText(0);
    try {
        return std::stof(val);
    } catch (...) {
        return defaultVal;
    }
}

void AudioSettingsRepository::loadInto(AudioEngine& engine)
{
    engine.setMasterVolume(getFloat("audio.master_volume", 1.0f));
    engine.setSfxVolume(getFloat("audio.sfx_volume", 1.0f));
    engine.setMusicVolume(getFloat("audio.music_volume", 1.0f));
    std::fprintf(stderr, "[AudioSettings] loaded: master=%.2f sfx=%.2f music=%.2f\n",
                 engine.masterVolume(), engine.sfxVolume(), engine.musicVolume());
}

void AudioSettingsRepository::save(const AudioEngine& engine)
{
    setFloat("audio.master_volume", engine.masterVolume());
    setFloat("audio.sfx_volume",    engine.sfxVolume());
    setFloat("audio.music_volume",  engine.musicVolume());
}
