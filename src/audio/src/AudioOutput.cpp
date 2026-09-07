#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <algorithm>
#include <cstring>

#include "comp/audio/AudioOutput.h"

namespace comp::audio {

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

void AudioOutput::setBuffer(const media::AudioBuffer* buffer) {
    stop();
    buffer_ = buffer;
    cursor_.store(0);
}

void AudioOutput::play(double fromSeconds) {
    if (buffer_ == nullptr || !buffer_->valid()) {
        return;
    }
    const auto frame = static_cast<std::uint64_t>(
        std::max(0.0, fromSeconds) * buffer_->sampleRate);
    cursor_.store(std::min<std::uint64_t>(frame, buffer_->frameCount()));
    playing_.store(true);
}

void AudioOutput::stop() { playing_.store(false); }

double AudioOutput::position() const noexcept {
    if (buffer_ == nullptr || buffer_->sampleRate <= 0) {
        return 0.0;
    }
    return static_cast<double>(cursor_.load()) / buffer_->sampleRate;
}

void AudioOutput::mix(float* out, std::uint32_t frames) {
    const media::AudioBuffer* buffer = buffer_;
    if (buffer == nullptr || !buffer->valid()) {
        return;
    }

    std::uint64_t at = cursor_.load();
    const std::uint64_t total = buffer->frameCount();
    const int channels = buffer->channels;

    for (std::uint32_t i = 0; i < frames; ++i) {
        if (at >= total) {
            // End of the track. Stop rather than looping: the transport owns looping,
            // and two things deciding when to wrap is how they disagree.
            playing_.store(false);
            break;
        }
        const std::size_t base = static_cast<std::size_t>(at) *
                                 static_cast<std::size_t>(channels);
        const float left = buffer->samples[base];
        const float right = (channels > 1) ? buffer->samples[base + 1] : left;
        out[i * 2] = left;
        out[i * 2 + 1] = right;
        ++at;
    }
    cursor_.store(at);
}

}  // namespace comp::audio
