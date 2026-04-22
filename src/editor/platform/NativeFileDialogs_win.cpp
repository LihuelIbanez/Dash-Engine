#include "platform/NativeFileDialogs.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>   // GetOpenFileNameA
#include <shobjidl.h>  // IFileOpenDialog (Vista+)
#include <shlobj.h>    // SHCreateItemFromParsingName

#include <string>
#include <filesystem>

namespace NativeFileDialogs {

// Open a file picker filtered to *.dashproject files.
std::string pickProjectPath(const std::string& initialPath)
{
    char buf[MAX_PATH] = {};

    OPENFILENAMEA ofn = {};
    ofn.lStructSize   = sizeof(ofn);
    ofn.hwndOwner     = nullptr;
    ofn.lpstrFilter   = "DashEngine Project\0*.dashproject\0All Files\0*.*\0";
    ofn.lpstrFile     = buf;
    ofn.nMaxFile      = MAX_PATH;
    ofn.lpstrTitle    = "Open DashEngine Project";
    ofn.Flags         = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (!initialPath.empty())
        ofn.lpstrInitialDir = initialPath.c_str();

    if (GetOpenFileNameA(&ofn))
        return std::string(buf);
    return {};
}

// Open a folder picker using the modern IFileOpenDialog (Vista+).
std::string pickProjectDirectory(const std::string& initialPath)
{
    // Ensure COM is initialised for this call (no-op if already initialised).
    bool comInited = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));

    std::string result;

    IFileOpenDialog* pfd = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        if (comInited) CoUninitialize();
        return {};
    }

    DWORD opts = 0;
    pfd->GetOptions(&opts);
    pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
    pfd->SetTitle(L"Select Project Folder");

    if (!initialPath.empty()) {
        // Convert narrow path to wide for the shell API.
        int wlen = MultiByteToWideChar(CP_UTF8, 0, initialPath.c_str(), -1, nullptr, 0);
        std::wstring wide(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, initialPath.c_str(), -1, wide.data(), wlen);

        IShellItem* psi = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(wide.c_str(), nullptr,
                                                  IID_PPV_ARGS(&psi)))) {
            pfd->SetFolder(psi);
            psi->Release();
        }
    }

    if (SUCCEEDED(pfd->Show(nullptr))) {
        IShellItem* psi = nullptr;
        if (SUCCEEDED(pfd->GetResult(&psi))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                int len = WideCharToMultiByte(CP_UTF8, 0, path, -1,
                                              nullptr, 0, nullptr, nullptr);
                if (len > 0) {
                    result.resize(static_cast<size_t>(len) - 1);
                    WideCharToMultiByte(CP_UTF8, 0, path, -1,
                                        result.data(), len, nullptr, nullptr);
                }
                CoTaskMemFree(path);
            }
            psi->Release();
        }
    }

    pfd->Release();
    if (comInited) CoUninitialize();
    return result;
}

} // namespace NativeFileDialogs
