#pragma once

#include <QElapsedTimer>
#include <QObject>

#include "comp/audio/AudioOutput.h"
#include "comp/engine/Transport.h"

class QTimer;

namespace comp::ui {

// Drives the transport from Qt's event loop.
//
// The timer fires faster than the frame rate on purpose. Qt timers are not precise, so
// a timer set to exactly one frame lands late most of the time and playback drifts slow.
// Ticking often and asking the transport how much real time passed decouples "how often
// we wake up" from "how fast the composition plays".
class Playback : public QObject {
    Q_OBJECT

public:
    explicit Playback(QObject* parent = nullptr);

    void configure(double duration, double frameRate);

    // Once a track is attached, the audio device becomes the clock. Passing null goes
    // back to the wall clock, which is the correct behaviour for a silent project.
    void setAudio(audio::AudioOutput* output);

    [[nodiscard]] bool playing() const noexcept { return transport_.playing(); }
    [[nodiscard]] double time() const noexcept { return transport_.time(); }

    // Frames actually delivered per second, smoothed. The honest number, not the target.
    [[nodiscard]] double measuredFps() const noexcept { return measuredFps_; }

public slots:
    void togglePlay();
    void stop();
    void seek(double seconds);

signals:
    void timeChanged(double seconds);
    void playingChanged(bool playing);

private:
    void tick();

    engine::Transport transport_;
    audio::AudioOutput* audio_ = nullptr;
    QTimer* timer_ = nullptr;
    QElapsedTimer clock_;
    double measuredFps_ = 0.0;
    qint64 lastElapsedNs_ = 0;  // for the wall-clock delta
    qint64 lastFrameNs_ = 0;    // for the measured frame rate
};

}  // namespace comp::ui
