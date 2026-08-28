#include "AudioSystem.h"

#include <cctype>
#include <filesystem>
#include <utility>

#include "AssetDatabase.h"
#include "AudioComponentBridge.h"
#include "AudioEmitter.h"
#include "AudioEngine.h"

namespace dash::audio {

bool looksLikeGuid(const std::string& s)
{
    if (s.size() != 36) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        const bool dashPos = (i == 8 || i == 13 || i == 18 || i == 23);
        if (dashPos) {
            if (s[i] != '-') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    return true;
}

std::string resolveClipPath(const std::string& clip, const AssetDatabase* assets)
{
    if (!assets || !looksLikeGuid(clip)) return clip;

    const AssetRecord* rec = assets->findByGuid(clip);
    if (!rec || rec->sourcePath.empty()) return clip;
    return rec->sourcePath;
}

}  // namespace dash::audio

AudioSystem::AudioSystem(AudioEngine* engine, const AssetDatabase* assets, std::string assetsRoot)
    : engine_(engine), assets_(assets), assetsRoot_(std::move(assetsRoot))
{
}

std::string AudioSystem::clipFilePath(const std::string& clip) const
{
    std::string path = dash::audio::resolveClipPath(clip, assets_);
    if (path.empty() || assetsRoot_.empty()) return path;

    const std::filesystem::path relative(path);
    if (relative.is_absolute()) return path;

    // Asset sourcePaths are relative to the assets root, and scene paths often are too.
    std::error_code ec;
    const std::filesystem::path underAssets = std::filesystem::path(assetsRoot_) / relative;
    if (std::filesystem::exists(underAssets, ec)) return underAssets.string();
    return path;
}

void AudioSystem::update(RuntimeContext& ctx)
{
    if (!engine_) return;

    // The isometric camera sits on the player, so the listener rides along.
    if (ctx.player) {
        const dash::audio::Vec3 pos =
            dash::audio::worldToAudio(ctx.player->x, ctx.player->y, ctx.player->z);
        const dash::audio::Vec3 fwd = dash::audio::isometricCameraForward();
        engine_->setListener(pos.x, pos.y, pos.z, fwd.x, fwd.y, fwd.z);
    }

    if (ctx.audioEmitters) {
        for (AudioEmitter& e : *ctx.audioEmitters) {
            if (!e.enabled) {
                if (e.voice != AudioEngine::kInvalidVoice) {
                    engine_->stopVoice(e.voice);
                    e.voice = AudioEngine::kInvalidVoice;
                }
                continue;
            }

            bool attachedAlive = true;

            switch (e.attachment) {
                case AudioEmitter::Attachment::Player:
                    if (ctx.player) {
                        e.x = ctx.player->x;
                        e.y = ctx.player->y;
                        e.z = ctx.player->z;
                        attachedAlive = ctx.player->isAlive();
                    } else {
                        attachedAlive = false;
                    }
                    break;

                case AudioEmitter::Attachment::Enemy: {
                    const bool bound = ctx.enemies && e.enemyIndex >= 0 &&
                                       static_cast<std::size_t>(e.enemyIndex) < ctx.enemies->size() &&
                                       (*ctx.enemies)[static_cast<std::size_t>(e.enemyIndex)];
                    if (bound) {
                        const Enemy& enemy = *(*ctx.enemies)[static_cast<std::size_t>(e.enemyIndex)];
                        e.x = enemy.x;
                        e.y = enemy.y;
                        e.z = enemy.z;
                        attachedAlive = enemy.isAlive();
                    } else {
                        attachedAlive = false;
                    }
                    break;
                }

                case AudioEmitter::Attachment::Static:
                    break;
            }

            if (!attachedAlive) {
                if (e.voice != AudioEngine::kInvalidVoice) {
                    engine_->stopVoice(e.voice);
                    e.voice = AudioEngine::kInvalidVoice;
                }
                continue;
            }

            const dash::audio::Vec3 pos = dash::audio::worldToAudio(e.x, e.y, e.z);

            if (!e.started) {
                // Latched even when the clip cannot be decoded, so a bad reference
                // is not retried on every frame.
                e.started = true;
                if (e.component.playOnStart && !e.component.clip.empty()) {
                    ++e.playCount;
                    const AudioEngine::SoundHandle sound =
                        engine_->loadSound(clipFilePath(e.component.clip));
                    if (sound != AudioEngine::kInvalidSound) {
                        e.voice = engine_->playSound(
                            sound, audioPlayParamsFrom(e.component, pos.x, pos.y, pos.z));
                    }
                }
            } else if (e.voice != AudioEngine::kInvalidVoice) {
                engine_->setVoicePosition(e.voice, pos.x, pos.y, pos.z);
            }

            // Drop handles of voices the engine already reaped so they are never reused.
            if (e.voice != AudioEngine::kInvalidVoice && !engine_->isVoicePlaying(e.voice))
                e.voice = AudioEngine::kInvalidVoice;
        }
    }

    engine_->update();
}
