#include "platform/NativeFileDialogs.h"

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace {

NSString* toNSString(const std::string& value)
{
    return [NSString stringWithUTF8String:value.c_str()];
}

std::string runOpenPanel(bool canChooseFiles,
                         bool canChooseDirectories,
                         bool canCreateDirectories,
                         const std::string& title,
                         const std::string& prompt,
                         const std::string& initialPath,
                         NSArray<NSString*>* allowedFileTypes)
{
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = canChooseFiles;
        panel.canChooseDirectories = canChooseDirectories;
        panel.canCreateDirectories = canCreateDirectories;
        panel.allowsMultipleSelection = NO;
        panel.resolvesAliases = YES;
        panel.title = toNSString(title);
        panel.prompt = toNSString(prompt);
        if (allowedFileTypes != nil) {
            if (@available(macOS 11.0, *)) {
                NSMutableArray<UTType*>* allowedContentTypes = [NSMutableArray array];
                for (NSString* fileType in allowedFileTypes) {
                    UTType* contentType = [UTType typeWithFilenameExtension:fileType];
                    if (contentType != nil) {
                        [allowedContentTypes addObject:contentType];
                    }
                }
                if (allowedContentTypes.count > 0) {
                    panel.allowedContentTypes = allowedContentTypes;
                }
            } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                panel.allowedFileTypes = allowedFileTypes;
#pragma clang diagnostic pop
            }
        }

        std::error_code ec;
        fs::path startPath = initialPath.empty() ? fs::current_path(ec) : fs::path(initialPath);
        if (!ec && !startPath.empty()) {
            if (!fs::exists(startPath, ec) && !startPath.parent_path().empty()) {
                startPath = startPath.parent_path();
            }
            if (!ec && fs::is_regular_file(startPath, ec)) {
                startPath = startPath.parent_path();
            }
            if (!ec && !startPath.empty() && fs::exists(startPath, ec)) {
                panel.directoryURL = [NSURL fileURLWithPath:toNSString(startPath.string())];
            }
        }

        if ([panel runModal] != NSModalResponseOK) {
            return {};
        }

        NSURL* url = panel.URL;
        return url != nil ? std::string(url.path.UTF8String) : std::string();
    }
}

} // namespace

namespace NativeFileDialogs {

std::string pickProjectPath(const std::string& initialPath)
{
    return runOpenPanel(true,
                        true,
                        false,
                        "Open Existing Project",
                        "Open",
                        initialPath,
                        @[ @"dashproject" ]);
}

std::string pickProjectDirectory(const std::string& initialPath)
{
    return runOpenPanel(false,
                        true,
                        true,
                        "Choose Project Folder",
                        "Select",
                        initialPath,
                        nil);
}

} // namespace NativeFileDialogs