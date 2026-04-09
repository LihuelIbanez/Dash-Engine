#pragma once

#include <cstdlib>
#include <string>

namespace DbMode {

enum class Mode {
    Json,
    Hybrid,
    Sqlite
};

inline Mode current()
{
    const char* raw = std::getenv("DASH_DB_MODE");
    if (!raw) return Mode::Hybrid;

    const std::string mode(raw);
    if (mode == "json") return Mode::Json;
    if (mode == "sqlite") return Mode::Sqlite;
    return Mode::Hybrid;
}

inline bool usesSqliteRead(Mode m)
{
    return m == Mode::Hybrid || m == Mode::Sqlite;
}

inline bool allowsJsonFallback(Mode m)
{
    return m == Mode::Hybrid;
}

inline bool writesJson(Mode m)
{
    return m == Mode::Json || m == Mode::Hybrid;
}

inline bool writesSqlite(Mode m)
{
    return m == Mode::Hybrid || m == Mode::Sqlite;
}

} // namespace DbMode
