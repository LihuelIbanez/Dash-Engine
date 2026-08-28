#include "AudioImporter.h"

#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kHeaderBytes = 16;

std::string lowercaseExtension(const fs::path& p)
{
    std::string ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

bool tagAt(const std::array<unsigned char, kHeaderBytes>& h, std::size_t offset, const char* tag)
{
    return std::memcmp(h.data() + offset, tag, std::strlen(tag)) == 0;
}

// MPEG audio frames start with 11 set sync bits; an ID3v2 tag is also common.
bool looksLikeMp3(const std::array<unsigned char, kHeaderBytes>& h)
{
    if (tagAt(h, 0, "ID3")) return true;
    return h[0] == 0xFF && (h[1] & 0xE0) == 0xE0;
}

// The extension is only a hint (renamed files, truncated downloads), so the
// magic bytes decide whether the container really is what it claims to be.
bool headerMatchesExtension(const std::array<unsigned char, kHeaderBytes>& h,
                            const std::string& ext,
                            std::string& outError)
{
    if (ext == ".wav") {
        if (!tagAt(h, 0, "RIFF") || !tagAt(h, 8, "WAVE")) {
            outError = "missing RIFF/WAVE header";
            return false;
        }
        return true;
    }
    if (ext == ".ogg") {
        if (!tagAt(h, 0, "OggS")) {
            outError = "missing OggS header";
            return false;
        }
        return true;
    }
    if (ext == ".flac") {
        if (!tagAt(h, 0, "fLaC")) {
            outError = "missing fLaC header";
            return false;
        }
        return true;
    }
    if (ext == ".mp3") {
        if (!looksLikeMp3(h)) {
            outError = "missing ID3 tag or MPEG frame sync";
            return false;
        }
        return true;
    }
    outError = "unsupported audio extension '" + ext + "'";
    return false;
}

} // namespace

bool AudioImporter::isAudioExtension(const std::string& lowercaseExt)
{
    return lowercaseExt == ".wav" || lowercaseExt == ".mp3" ||
           lowercaseExt == ".flac" || lowercaseExt == ".ogg";
}

ImportResult AudioImporter::import(const std::string& sourcePath,
                                   const std::string& outputPath,
                                   AssetRecord& record)
{
    ImportResult result;

    const fs::path src(sourcePath);
    std::error_code ec;
    if (!fs::is_regular_file(src, ec)) {
        result.errors.push_back("Audio file not found: " + sourcePath);
        return result;
    }

    const std::string ext = lowercaseExtension(src);
    if (!isAudioExtension(ext)) {
        result.errors.push_back("AudioImporter does not handle '" + ext + "': " + sourcePath);
        return result;
    }

    const auto size = fs::file_size(src, ec);
    if (ec || size == 0) {
        result.errors.push_back("Audio file is empty: " + sourcePath);
        return result;
    }

    std::ifstream in(src, std::ios::binary);
    if (!in.is_open()) {
        result.errors.push_back("Cannot open audio file: " + sourcePath);
        return result;
    }

    std::array<unsigned char, kHeaderBytes> header{};
    in.read(reinterpret_cast<char*>(header.data()), kHeaderBytes);
    const std::streamsize read = in.gcount();
    in.close();

    if (read < static_cast<std::streamsize>(kHeaderBytes)) {
        result.errors.push_back("Audio file is truncated (" + std::to_string(read) +
                                " bytes): " + sourcePath);
        return result;
    }

    std::string headerError;
    if (!headerMatchesExtension(header, ext, headerError)) {
        result.errors.push_back("Corrupt or mislabelled audio file (" + headerError +
                                "): " + sourcePath);
        return result;
    }

    fs::create_directories(fs::path(outputPath).parent_path(), ec);
    if (ec) {
        result.errors.push_back("Failed to create output directory: " + ec.message());
        return result;
    }
    fs::copy_file(src, fs::path(outputPath), fs::copy_options::overwrite_existing, ec);
    if (ec) {
        result.errors.push_back("Failed to copy audio to library: " + ec.message());
        return result;
    }

    record.assetType = AssetType::Audio;
    result.success   = true;
    return result;
}
