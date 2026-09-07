#include "comp/media/AudioDecoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>

namespace comp::media {

float AudioBuffer::monoAt(std::size_t frame) const noexcept {
    if (channels <= 0 || frame >= frameCount()) {
        return 0.0f;
    }
    const std::size_t base = frame * static_cast<std::size_t>(channels);
    float sum = 0.0f;
    for (int c = 0; c < channels; ++c) {
        sum += samples[base + static_cast<std::size_t>(c)];
    }
    return sum / static_cast<float>(channels);
}

std::optional<AudioBuffer> AudioDecoder::decode(const std::string& path, int targetRate) {
    // Same reason as VideoDecoder: FFmpeg's chatter reads like failure and is not.
    static const bool quieted = [] {
        av_log_set_level(AV_LOG_FATAL);
        return true;
    }();
    (void)quieted;

    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, path.c_str(), nullptr, nullptr) < 0) {
        return std::nullopt;
    }
    struct FormatGuard {
        AVFormatContext** f;
        ~FormatGuard() { avformat_close_input(f); }
    } formatGuard{&format};

    if (avformat_find_stream_info(format, nullptr) < 0) {
        return std::nullopt;
    }

    const AVCodec* decoder = nullptr;
    const int index =
        av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (index < 0 || decoder == nullptr) {
        return std::nullopt;  // no audio stream; a legal state, not an error
    }

    AVStream* stream = format->streams[index];
    AVCodecContext* codec = avcodec_alloc_context3(decoder);
    if (codec == nullptr) {
        return std::nullopt;
    }
    struct CodecGuard {
        AVCodecContext** c;
        ~CodecGuard() { avcodec_free_context(c); }
    } codecGuard{&codec};

    if (avcodec_parameters_to_context(codec, stream->codecpar) < 0 ||
        avcodec_open2(codec, decoder, nullptr) < 0) {
        return std::nullopt;
    }

    const int channels = std::min(codec->ch_layout.nb_channels, 2);
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, channels);

    SwrContext* swr = nullptr;
    if (swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, targetRate,
                            &codec->ch_layout, codec->sample_fmt, codec->sample_rate, 0,
                            nullptr) < 0 ||
        swr_init(swr) < 0) {
        av_channel_layout_uninit(&outLayout);
        return std::nullopt;
    }
    struct SwrGuard {
        SwrContext** s;
        ~SwrGuard() { swr_free(s); }
    } swrGuard{&swr};

    AudioBuffer out;
    out.sampleRate = targetRate;
    out.channels = channels;
    if (stream->duration > 0) {
        const double seconds =
            static_cast<double>(stream->duration) * av_q2d(stream->time_base);
        out.samples.reserve(static_cast<std::size_t>(seconds * targetRate * channels));
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    std::vector<float> scratch;

    const auto drain = [&](AVFrame* in) {
        // Ask swr for the worst case: whatever it has buffered plus this frame.
        const int maxOut = static_cast<int>(
            av_rescale_rnd(swr_get_delay(swr, codec->sample_rate) +
                               (in != nullptr ? in->nb_samples : 0),
                           targetRate, codec->sample_rate, AV_ROUND_UP));
        if (maxOut <= 0) {
            return;
        }
        scratch.resize(static_cast<std::size_t>(maxOut) *
                       static_cast<std::size_t>(channels));
        auto* dst = reinterpret_cast<std::uint8_t*>(scratch.data());
        const int got = swr_convert(swr, &dst, maxOut,
                                    in != nullptr ? in->extended_data : nullptr,
                                    in != nullptr ? in->nb_samples : 0);
        if (got > 0) {
            out.samples.insert(out.samples.end(), scratch.begin(),
                               scratch.begin() + static_cast<std::size_t>(got) * channels);
        }
    };

    while (av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == index && avcodec_send_packet(codec, packet) >= 0) {
            while (avcodec_receive_frame(codec, frame) == 0) {
                drain(frame);
            }
        }
        av_packet_unref(packet);
    }
    avcodec_send_packet(codec, nullptr);
    while (avcodec_receive_frame(codec, frame) == 0) {
        drain(frame);
    }
    drain(nullptr);  // flush the resampler's tail

    av_frame_free(&frame);
    av_packet_free(&packet);
    av_channel_layout_uninit(&outLayout);

    if (out.samples.empty()) {
        return std::nullopt;
    }
    return out;
}

WaveformPeaks AudioDecoder::peaks(const AudioBuffer& buffer, double bucketsPerSecond) {
    WaveformPeaks result;
    if (!buffer.valid() || bucketsPerSecond <= 0.0) {
        return result;
    }

    result.bucketsPerSecond = bucketsPerSecond;
    const auto perBucket = std::max<std::size_t>(
        1, static_cast<std::size_t>(buffer.sampleRate / bucketsPerSecond));
    const std::size_t frames = buffer.frameCount();
    const std::size_t count = (frames + perBucket - 1) / perBucket;

    result.low.resize(count);
    result.high.resize(count);

    for (std::size_t b = 0; b < count; ++b) {
        const std::size_t begin = b * perBucket;
        const std::size_t end = std::min(begin + perBucket, frames);
        float lo = 0.0f;
        float hi = 0.0f;
        for (std::size_t f = begin; f < end; ++f) {
            const float v = buffer.monoAt(f);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        result.low[b] = lo;
        result.high[b] = hi;
    }
    return result;
}

}  // namespace comp::media
