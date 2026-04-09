#pragma once

#include <string>

namespace dash::vkexp {

class VulkanDiagnostics {
public:
    static std::string buildReport();
};

} // namespace dash::vkexp
