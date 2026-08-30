#include "rendering/textures/ExrImage.h"

#ifdef DASH_HAVE_OPENEXR

#include <ImfChannelList.h>
#include <ImfFrameBuffer.h>
#include <ImfInputFile.h>
#include <ImathBox.h>

#include <algorithm>
#include <cstdio>

namespace dash::vkexp {
namespace {

// DWAA decompresses in blocks of 32 scanlines; a strip that covers two blocks
// keeps the reader sequential without holding more than ~1.5 MB of a 4K image.
constexpr int kStripRows = 64;

const char* kChannelNames[3] = {"R", "G", "B"};

// A greyscale EXR has no "R": OpenEXR stores single-channel luminance as "Y".
const char* singleChannelName(const Imf::ChannelList& chans)
{
    for (const char* name : {"R", "Y", "G"}) {
        if (chans.findChannel(name)) return name;
    }
    auto it = chans.begin();
    return it != chans.end() ? it.name() : nullptr;
}

} // namespace

bool exrSupportAvailable() { return true; }

bool loadExrDownsampled(const std::string& path, int channels, uint32_t dstExtent,
                        std::vector<float>& out)
{
    if (channels != 1 && channels != 3) return false;
    if (dstExtent == 0) return false;

    try {
        Imf::InputFile file(path.c_str());
        const Imath::Box2i dw = file.header().dataWindow();
        const int srcW = dw.max.x - dw.min.x + 1;
        const int srcH = dw.max.y - dw.min.y + 1;
        if (srcW <= 0 || srcH <= 0) return false;

        const Imf::ChannelList& chans = file.header().channels();
        const char* names[3] = {kChannelNames[0], kChannelNames[1], kChannelNames[2]};
        if (channels == 1) {
            names[0] = singleChannelName(chans);
            if (names[0] == nullptr) {
                std::fprintf(stderr, "[Exr] %s: no usable channel\n", path.c_str());
                return false;
            }
        } else {
            for (int c = 0; c < channels; ++c) {
                if (!chans.findChannel(names[c])) {
                    std::fprintf(stderr, "[Exr] %s: missing channel %s\n",
                                 path.c_str(), names[c]);
                    return false;
                }
            }
        }

        const size_t dstPixels = static_cast<size_t>(dstExtent) * dstExtent;
        std::vector<double> acc(dstPixels * static_cast<size_t>(channels), 0.0);
        std::vector<uint32_t> counts(dstPixels, 0);

        // Source column -> destination column, precomputed once per file.
        std::vector<uint32_t> colMap(static_cast<size_t>(srcW));
        for (int x = 0; x < srcW; ++x) {
            colMap[static_cast<size_t>(x)] = std::min<uint32_t>(
                dstExtent - 1,
                static_cast<uint32_t>(static_cast<uint64_t>(x) * dstExtent / static_cast<uint64_t>(srcW)));
        }

        const size_t rowFloats = static_cast<size_t>(srcW) * static_cast<size_t>(channels);
        std::vector<float> strip(rowFloats * kStripRows);
        const ptrdiff_t xStride = static_cast<ptrdiff_t>(sizeof(float)) * channels;
        const ptrdiff_t yStride = static_cast<ptrdiff_t>(sizeof(float)) * static_cast<ptrdiff_t>(rowFloats);

        for (int y0 = 0; y0 < srcH; y0 += kStripRows) {
            const int y1 = std::min(y0 + kStripRows - 1, srcH - 1);

            Imf::FrameBuffer fb;
            for (int c = 0; c < channels; ++c) {
                char* base = reinterpret_cast<char*>(strip.data() + c)
                           - static_cast<ptrdiff_t>(dw.min.x) * xStride
                           - static_cast<ptrdiff_t>(dw.min.y + y0) * yStride;
                fb.insert(names[c], Imf::Slice(Imf::FLOAT, base, static_cast<size_t>(xStride),
                                               static_cast<size_t>(yStride)));
            }
            file.setFrameBuffer(fb);
            file.readPixels(dw.min.y + y0, dw.min.y + y1);

            for (int y = y0; y <= y1; ++y) {
                const uint32_t dy = std::min<uint32_t>(
                    dstExtent - 1,
                    static_cast<uint32_t>(static_cast<uint64_t>(y) * dstExtent / static_cast<uint64_t>(srcH)));
                const float* src = strip.data() + rowFloats * static_cast<size_t>(y - y0);
                for (int x = 0; x < srcW; ++x) {
                    const size_t di = static_cast<size_t>(dy) * dstExtent + colMap[static_cast<size_t>(x)];
                    double* a = acc.data() + di * static_cast<size_t>(channels);
                    const float* s = src + static_cast<size_t>(x) * static_cast<size_t>(channels);
                    for (int c = 0; c < channels; ++c) a[c] += static_cast<double>(s[c]);
                    ++counts[di];
                }
            }
        }

        out.assign(dstPixels * static_cast<size_t>(channels), 0.0f);
        for (size_t i = 0; i < dstPixels; ++i) {
            const double n = counts[i] > 0 ? static_cast<double>(counts[i]) : 1.0;
            for (int c = 0; c < channels; ++c)
                out[i * static_cast<size_t>(channels) + static_cast<size_t>(c)] =
                    static_cast<float>(acc[i * static_cast<size_t>(channels) + static_cast<size_t>(c)] / n);
        }
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[Exr] %s: %s\n", path.c_str(), e.what());
        return false;
    }
}

} // namespace dash::vkexp

#else // !DASH_HAVE_OPENEXR

namespace dash::vkexp {

bool exrSupportAvailable() { return false; }

bool loadExrDownsampled(const std::string&, int, uint32_t, std::vector<float>&) { return false; }

} // namespace dash::vkexp

#endif
