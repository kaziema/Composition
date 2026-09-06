// Decodes real frames from a real file. Needs a video to point at, so it takes a path
// from COMP_TEST_VIDEO and reports a skip when there isn't one. A test that silently
// passes is worse than one that says it did nothing.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "comp/media/VideoDecoder.h"

using namespace comp::media;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// A frame that decoded but came out uniformly black usually means the colour conversion
// silently did nothing, which is easy to miss and looks like "no video".
bool hasContent(const VideoFrame& frame) {
    std::uint8_t low = 255;
    std::uint8_t high = 0;
    for (std::size_t i = 0; i < frame.rgba.size(); i += 4) {
        low = std::min(low, frame.rgba[i]);
        high = std::max(high, frame.rgba[i]);
    }
    return high > low + 8;
}

}  // namespace

int main() {
    const char* path = std::getenv("COMP_TEST_VIDEO");
    if (path == nullptr || *path == '\0') {
        std::puts("media: SKIPPED (set COMP_TEST_VIDEO to a video file)");
        return EXIT_SUCCESS;
    }

    auto decoder = VideoDecoder::open(path);
    check(decoder != nullptr, "the file opens");
    if (decoder == nullptr) {
        std::fprintf(stderr, "could not open %s\n", path);
        return EXIT_FAILURE;
    }

    std::printf("opened %dx%d, %.3f fps, %.2fs\n", decoder->width(), decoder->height(),
                decoder->fps(), decoder->duration());

    check(decoder->width() > 0 && decoder->height() > 0, "the file reports its size");
    check(decoder->fps() > 0.0, "the file reports a frame rate");
    check(decoder->duration() > 0.0, "the file reports a duration");

    const VideoFrame* first = decoder->frameAt(1.0);
    check(first != nullptr, "a frame decodes at 1s");
    if (first != nullptr) {
        check(first->valid(), "the frame has pixels");
        check(first->width == decoder->width(), "frame width matches the stream");
        check(first->rgba.size() ==
                  static_cast<std::size_t>(first->width) *
                      static_cast<std::size_t>(first->height) * 4,
              "the buffer is tightly packed RGBA");
        check(hasContent(*first), "the frame is not uniformly flat");
    }

    // Seeking forward, then backward. Backward is the case a naive decoder gets wrong,
    // because it cannot rewind without seeking to a keyframe first.
    const VideoFrame* later = decoder->frameAt(3.0);
    check(later != nullptr && later->valid(), "a later frame decodes");
    const double laterPts = (later != nullptr) ? later->pts : -1.0;

    const VideoFrame* back = decoder->frameAt(0.5);
    check(back != nullptr && back->valid(), "seeking backwards still decodes");
    if (back != nullptr) {
        check(back->pts < laterPts, "the rewound frame is genuinely earlier");
    }

    // Asking for the same instant twice should not re-seek or change the answer.
    const VideoFrame* again = decoder->frameAt(0.5);
    check(again != nullptr && again->valid(), "re-requesting the same time works");

    if (failures != 0) {
        std::fprintf(stderr, "\n%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("media: all checks passed");
    return EXIT_SUCCESS;
}
