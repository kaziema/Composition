#include "ruby/ui/Playback.h"

#include <QTimer>
#include <algorithm>

namespace ruby::ui {

Playback::Playback(QObject* parent) : QObject(parent) {
    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
    connect(timer_, &QTimer::timeout, this, &Playback::tick);
}

void Playback::configure(double duration, double frameRate) {
    transport_.setDuration(duration);
    transport_.setFrameRate(frameRate);

    // Wake up about four times per frame. Enough to land close to each boundary without
    // spinning the CPU, and the transport corrects for whatever the real interval was.
    const double frameMs = 1000.0 / transport_.frameRate();
    timer_->setInterval(std::max(1, static_cast<int>(frameMs / 4.0)));
}

void Playback::setAudio(audio::AudioOutput* output) { audio_ = output; }

void Playback::togglePlay() {
    transport_.toggle();
    if (transport_.playing()) {
        clock_.restart();
        lastElapsedNs_ = 0;
        lastFrameNs_ = 0;
        if (audio_ != nullptr) {
            audio_->play(transport_.time());
        }
        timer_->start();
    } else {
        if (audio_ != nullptr) {
            audio_->stop();
        }
        timer_->stop();
        measuredFps_ = 0.0;
    }
    emit playingChanged(transport_.playing());
}

void Playback::stop() {
    if (!transport_.playing()) {
        return;
    }
    togglePlay();
}

void Playback::seek(double seconds) {
    transport_.setTime(seconds);
    if (audio_ != nullptr && transport_.playing()) {
        audio_->play(transport_.time());  // re-cue the device to the new position
    }
    emit timeChanged(transport_.time());
}

void Playback::tick() {
    const qint64 nowNs = clock_.nsecsElapsed();
    const int before = transport_.frame();

    // With audio playing, the device is the clock. An audio card runs on its own
    // crystal and will not match the system clock exactly; following wall clock here
    // would let picture and sound drift apart over a long take, which is fatal in an
    // app whose whole premise is landing cuts on the beat.
    const bool audioDriving = audio_ != nullptr && audio_->playing();
    if (audioDriving) {
        transport_.setTime(audio_->position());
    } else {
        const double elapsed = static_cast<double>(nowNs - lastElapsedNs_) / 1e9;
        transport_.advance(elapsed);
    }
    lastElapsedNs_ = nowNs;

    // The audio device stops itself at the end of the track; the transport owns what
    // happens next, so ask it to wrap or halt and re-cue the device to match.
    if (audio_ != nullptr && transport_.playing() && !audio_->playing()) {
        if (transport_.looping()) {
            transport_.setTime(0.0);
            audio_->play(0.0);
        } else {
            transport_.stop();
        }
    }

    if (transport_.frame() == before && transport_.playing()) {
        return;  // still inside the same frame; nothing to redraw
    }

    if (lastFrameNs_ != 0) {
        const double delta = static_cast<double>(nowNs - lastFrameNs_) / 1e9;
        if (delta > 0.0) {
            // Exponential smoothing: the raw number jitters too much to read.
            const double instant = 1.0 / delta;
            measuredFps_ = (measuredFps_ <= 0.0) ? instant
                                                 : measuredFps_ * 0.9 + instant * 0.1;
        }
    }
    lastFrameNs_ = nowNs;

    emit timeChanged(transport_.time());

    if (!transport_.playing()) {
        timer_->stop();
        measuredFps_ = 0.0;
        emit playingChanged(false);
    }
}

}  // namespace ruby::ui
