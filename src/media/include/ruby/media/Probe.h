#pragma once

#include <optional>
#include <string>

namespace ruby::media {

// What a file turns out to contain. Read on import by opening the container and looking
// at its stream headers, without decoding anything: a four minute 4K file should not
// take four seconds to appear in the project panel.
struct MediaInfo {
    bool hasVideo = false;
    bool hasAudio = false;

    int width = 0;
    int height = 0;
    double fps = 0.0;

    int sampleRate = 0;
    int channels = 0;

    double duration = 0.0;  // seconds, longest stream

    [[nodiscard]] bool valid() const noexcept { return hasVideo || hasAudio; }
};

// Nullopt when the file cannot be opened or holds nothing we can use.
[[nodiscard]] std::optional<MediaInfo> probe(const std::string& path);

}  // namespace ruby::media
