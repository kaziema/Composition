#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <algorithm>
#include <cstring>

#include "ruby/audio/AudioOutput.h"

namespace ruby::audio {

class AudioOutput::Impl {
public:
    ma_device device{};
    bool started = false;
};

namespace {

constexpr int kDeviceRate = 48000;
constexpr int kDeviceChannels = 2;

// Real-time thread. No allocation, no locks, no blocking. Anything that could wait
// belongs somewhere else.
void dataCallback(ma_device* device, void* output, const void*, ma_uint32 frameCount) {
    auto* self = static_cast<AudioOutput*>(device->pUserData);
    auto* out = static_cast<float*>(output);
    const std::size_t wanted = static_cast<std::size_t>(frameCount) * kDeviceChannels;
    std::memset(out, 0, wanted * sizeof(float));

    if (self == nullptr || !self->playing()) {
        return;
    }
    self->mix(out, frameCount);
}

}  // namespace

AudioOutput::AudioOutput() : impl_(std::make_unique<Impl>()) {}

AudioOutput::~AudioOutput() {
    if (impl_ && impl_->started) {
        ma_device_uninit(&impl_->device);
    }
}

std::unique_ptr<AudioOutput> AudioOutput::create() {
    std::unique_ptr<AudioOutput> self(new AudioOutput());

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = kDeviceChannels;
    config.sampleRate = kDeviceRate;
    config.dataCallback = dataCallback;
    config.pUserData = self.get();

    if (ma_device_init(nullptr, &config, &self->impl_->device) != MA_SUCCESS) {
        return nullptr;
    }
    if (ma_device_start(&self->impl_->device) != MA_SUCCESS) {
        ma_device_uninit(&self->impl_->device);
        return nullptr;
    }
    self->impl_->started = true;
    return self;
}

void AudioOutput::setSources(const std::vector<AudioSource>& sources) {
    const int count = std::min(static_cast<int>(sources.size()), kMaxSources);

    // Odd sequence means "being written". A reader that sees an odd number, or a number
    // that moved while it was copying, keeps what it already had for this callback.
    seq_.fetch_add(1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);

    for (int i = 0; i < count; ++i) {
        sources_[i] = sources[static_cast<std::size_t>(i)];
    }
    sourceCount_ = count;

    std::atomic_thread_fence(std::memory_order_release);
    seq_.fetch_add(1, std::memory_order_release);
}

void AudioOutput::setBuffer(const media::AudioBuffer* buffer) {
    if (buffer == nullptr || !buffer->valid()) {
        setSources({});
        return;
    }
    AudioSource source;
    source.buffer = buffer;
    source.startSeconds = 0.0;
    source.endSeconds = buffer->duration();
    setSources({source});
}

void AudioOutput::play(double fromSeconds) {
    cursor_.store(static_cast<std::uint64_t>(std::max(0.0, fromSeconds) * kDeviceRate));
    playing_.store(true);
}

void AudioOutput::stop() { playing_.store(false); }

double AudioOutput::position() const noexcept {
    return static_cast<double>(cursor_.load()) / kDeviceRate;
}

void AudioOutput::mix(float* out, std::uint32_t frames) {
    // Refresh the private copy only if the list actually changed. Reading a stable list
    // costs one atomic load per callback.
    const std::uint32_t before = seq_.load(std::memory_order_acquire);
    if (before != snapshotSeq_ && (before % 2) == 0) {
        AudioSource candidate[kMaxSources];
        const int count = sourceCount_;
        for (int i = 0; i < count && i < kMaxSources; ++i) {
            candidate[i] = sources_[i];
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq_.load(std::memory_order_acquire) == before) {
            // The write finished before we started and did not start again while we
            // copied, so this snapshot is whole.
            for (int i = 0; i < count && i < kMaxSources; ++i) {
                snapshot_[i] = candidate[i];
            }
            snapshotCount_ = std::min(count, kMaxSources);
            snapshotSeq_ = before;
        }
        // Otherwise keep the previous snapshot. One callback of staleness is inaudible;
        // half a source list is not.
    }

    if (snapshotCount_ == 0) {
        return;
    }

    const std::uint64_t start = cursor_.load();
    for (std::uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(start + i) / kDeviceRate;

        float left = 0.0f;
        float right = 0.0f;
        for (int sIndex = 0; sIndex < snapshotCount_; ++sIndex) {
            const AudioSource& source = snapshot_[sIndex];
            if (source.buffer == nullptr || t < source.startSeconds ||
                t >= source.endSeconds) {
                continue;
            }
            // Index through the source's own rate rather than the device's, so a buffer
            // that was decoded at a different rate plays at the right pitch instead of
            // being transposed.
            const double into = t - source.startSeconds + source.sourceOffset;
            const auto frame =
                static_cast<std::uint64_t>(into * source.buffer->sampleRate);
            if (into < 0.0 || frame >= source.buffer->frameCount()) {
                continue;
            }
            const int channels = source.buffer->channels;
            const std::size_t base =
                static_cast<std::size_t>(frame) * static_cast<std::size_t>(channels);
            const float l = source.buffer->samples[base];
            const float r = (channels > 1) ? source.buffer->samples[base + 1] : l;
            left += l * source.gain;
            right += r * source.gain;
        }

        // Summing layers can exceed full scale. Clamping is not mixing, but a hard clip
        // is far better than the wrap-around fuzz you get from letting it overflow, and
        // it is honest: it sounds like something is too loud, because it is.
        out[i * 2] = std::clamp(left, -1.0f, 1.0f);
        out[i * 2 + 1] = std::clamp(right, -1.0f, 1.0f);
    }

    // Runs on past the last source rather than stopping. With several layers there can be
    // silence between them, and stopping at the first gap would end playback in the
    // middle of a composition. The transport decides when playing is over.
    cursor_.store(start + frames);
}

}  // namespace ruby::audio
