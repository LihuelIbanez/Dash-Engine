#pragma once
#include <string>
#include <vector>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
// FileEditorPanel — File Browser + tabbed source file editor with undo/redo.
// Extracted from EditorApp to keep the main editor class smaller.
// ─────────────────────────────────────────────────────────────────────────────
class FileEditorPanel {
public:
    using LogCallback = std::function<void(const std::string&)>;

    // Sets the initial browse root. Call once during editor startup.
    void init(const std::string& resourcesDir);

    // Opens `path` in a new tab, or focuses it if already open.
    void openFile(const std::string& path, const LogCallback& logCb);

    void drawFileBrowser(const std::string& resourcesDir,
                        const std::string& assetsRoot,
                        const std::string& scenesDir,
                        const LogCallback& logCb);
    void drawFileEditor(const LogCallback& logCb);

private:
    struct OpenFile {
        std::string path;
        std::string content;
        bool        modified = false;
        // Undo / redo stacks
        std::vector<std::string> undoStack;
        std::vector<std::string> redoStack;
        std::string              lastSnapshot; // last committed state
    };

    void saveFile(int idx, const LogCallback& logCb);
    void snapshotForUndo(OpenFile& f);
    void undoFile(OpenFile& f);
    void redoFile(OpenFile& f);

    std::vector<OpenFile> openFiles_;
    int                   activeFileTab_ = -1;
    std::string           fileBrowserRoot_;
    char                  fileBrowserNavBuf_[512] = {};
    char                  fileBrowserFilter_[128] = {};
};
