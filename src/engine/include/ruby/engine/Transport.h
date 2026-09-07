#pragma once

namespace ruby::engine {

// Playback state and the clock rule that governs it.
//
// Deliberately has no timer in it. Time advances by however much wall clock actually
// elapsed, which is the only way playback stays honest when frames are late: a slow
// frame costs you a dropped frame, not a slow-motion composition. A fixed per-tick
// increment would silently turn a struggling decoder into wrong-speed playback, and
// that is a bug you would never notice until you exported.
class Transport {
public:
    void setDuration(double seconds) noexcept;
    void setFrameRate(double fps) noexcept;

    [[nodiscard]] double duration() const noexcept { return duration_; }
    [[nodiscard]] double frameRate() const noexcept { return fps_; }

    [[nodiscard]] double time() const noexcept { return time_; }
    void setTime(double seconds) noexcept;

    [[nodiscard]] bool playing() const noexcept { return playing_; }
    void play() noexcept;
    void stop() noexcept;
    void toggle() noexcept;

    [[nodiscard]] bool looping() const noexcept { return looping_; }
    void setLooping(bool loop) noexcept { looping_ = loop; }

    // Moves time forward by real elapsed seconds. Returns true when the frame that
    // should be on screen changed, which is the only time a repaint is worth doing.
    bool advance(double elapsedSeconds) noexcept;

    // Index of the frame currently displayed.
    [[nodiscard]] int frame() const noexcept;

private:
    double duration_ = 12.0;
    double fps_ = 30.0;
    double time_ = 0.0;
    bool playing_ = false;
    bool looping_ = true;
};

}  // namespace ruby::engine
