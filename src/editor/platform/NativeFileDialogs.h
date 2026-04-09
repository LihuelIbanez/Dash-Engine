#pragma once

#include <string>

namespace NativeFileDialogs {

std::string pickProjectPath(const std::string& initialPath = {});
std::string pickProjectDirectory(const std::string& initialPath = {});

}