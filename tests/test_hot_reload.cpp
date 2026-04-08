// ═════════════════════════════════════════════════════════════════════════════
// test_hot_reload — FileWatcher: Added / Modified / Deleted / no-change / reset
// ═════════════════════════════════════════════════════════════════════════════
#include "FileWatcher.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

// Helper: write content to a file, creating parent dirs if needed.
static void writeFile(const fs::path& p, const char* content)
{
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
}

// Helper: count changes of a given type.
static int countChanges(const std::vector<FileWatcher::FileChange>& changes,
                        FileWatcher::FileChange::Type type)
{
    int n = 0;
    for (auto& c : changes)
        if (c.type == type) ++n;
    return n;
}

// Helper: find change by relative path.
static bool hasChange(const std::vector<FileWatcher::FileChange>& changes,
                      const std::string& relPath,
                      FileWatcher::FileChange::Type type)
{
    for (auto& c : changes)
        if (c.relativePath == relPath && c.type == type) return true;
    return false;
}

// ── Test: create FileWatcher over temp directory ──────────────────────────────
static void test_create_watcher(const fs::path& tmpDir)
{
    std::printf("  test_create_watcher\n");

    FileWatcher fw(tmpDir.string(), 0.0f);
    fw.reset();   // establish baseline on empty dir

    ASSERT(fw.changes().empty(), "no changes after reset on empty dir");
}

// ── Test: add file → scan detects Added ──────────────────────────────────────
static void test_detect_added(const fs::path& tmpDir)
{
    std::printf("  test_detect_added\n");

    FileWatcher fw(tmpDir.string(), 0.0f);
    fw.reset();   // baseline: empty dir

    writeFile(tmpDir / "added.txt", "hello world");

    fw.scan();
    const auto& changes = fw.changes();
    ASSERT(countChanges(changes, FileWatcher::FileChange::Added) == 1, "one Added change");
    ASSERT(hasChange(changes, "added.txt", FileWatcher::FileChange::Added),
           "added.txt reported as Added");
}

// ── Test: modify file → scan detects Modified ────────────────────────────────
static void test_detect_modified(const fs::path& tmpDir)
{
    std::printf("  test_detect_modified\n");

    // Write initial file and establish baseline
    writeFile(tmpDir / "data.json", R"({"v":1})");

    FileWatcher fw(tmpDir.string(), 0.0f);
    fw.reset();   // baseline includes data.json

    // Modify the file
    writeFile(tmpDir / "data.json", R"({"v":2,"extra":"changed"})");

    fw.scan();
    const auto& changes = fw.changes();
    ASSERT(countChanges(changes, FileWatcher::FileChange::Modified) >= 1, "at least one Modified");
    ASSERT(hasChange(changes, "data.json", FileWatcher::FileChange::Modified),
           "data.json reported as Modified");
}

// ── Test: delete file → scan detects Deleted ─────────────────────────────────
static void test_detect_deleted(const fs::path& tmpDir)
{
    std::printf("  test_detect_deleted\n");

    writeFile(tmpDir / "todelete.txt", "bye");

    FileWatcher fw(tmpDir.string(), 0.0f);
    fw.reset();   // baseline includes todelete.txt

    fs::remove(tmpDir / "todelete.txt");

    fw.scan();
    const auto& changes = fw.changes();
    ASSERT(countChanges(changes, FileWatcher::FileChange::Deleted) >= 1, "at least one Deleted");
    ASSERT(hasChange(changes, "todelete.txt", FileWatcher::FileChange::Deleted),
           "todelete.txt reported as Deleted");
}

// ── Test: no changes → scan returns 0 changes ────────────────────────────────
static void test_no_changes(const fs::path& tmpDir)
{
    std::printf("  test_no_changes\n");

    writeFile(tmpDir / "stable.txt", "unchanged");

    FileWatcher fw(tmpDir.string(), 0.0f);
    fw.reset();   // baseline includes stable.txt

    fw.scan();
    ASSERT(fw.changes().empty(), "no changes when files unchanged");
}

// ── Test: reset() establishes new baseline ───────────────────────────────────
static void test_reset_baseline(const fs::path& tmpDir)
{
    std::printf("  test_reset_baseline\n");

    FileWatcher fw(tmpDir.string(), 0.0f);
    fw.reset();   // baseline on current state

    // Add a new file BEFORE next reset
    writeFile(tmpDir / "newfile.txt", "new");

    // Reset again — new file is now part of baseline, not a pending change
    fw.reset();
    ASSERT(fw.changes().empty(), "changes cleared after reset()");

    // scan() should show no changes since newfile.txt is now in baseline
    fw.scan();
    bool foundAdded = hasChange(fw.changes(), "newfile.txt", FileWatcher::FileChange::Added);
    ASSERT(!foundAdded, "newfile.txt not reported after it was included in reset baseline");
}

// ── main ─────────────────────────────────────────────────────────────────────
int main()
{
    std::printf("=== test_hot_reload ===\n");

    // Create a fresh temp directory for each test to avoid cross-contamination.
    auto runTest = [](const char* name, auto fn) {
        fs::path tmp = fs::temp_directory_path() / ("dash_fw_test_" + std::string(name));
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        fn(tmp);
        fs::remove_all(tmp);
    };

    runTest("create",   test_create_watcher);
    runTest("added",    test_detect_added);
    runTest("modified", test_detect_modified);
    runTest("deleted",  test_detect_deleted);
    runTest("nochange", test_no_changes);
    runTest("reset",    test_reset_baseline);

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
