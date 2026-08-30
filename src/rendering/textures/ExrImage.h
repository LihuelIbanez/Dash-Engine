#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dash::vkexp {

// True when the build found OpenEXR. Without it every load below fails and the
// callers fall back to their procedural substitutes.
bool exrSupportAvailable();

// Decodes `path` directly into a `dstExtent` x `dstExtent` box-filtered buffer,
// consuming the source in scanline strips so a 4K image never materialises in
// full. `channels` is 1 (R) or 3 (RGB interleaved); values stay as authored.
bool loadExrDownsampled(const std::string& path,
                        int channels,
                        uint32_t dstExtent,
                        std::vector<float>& out);

} // namespace dash::vkexp
