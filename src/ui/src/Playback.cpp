#include "comp/ui/Playback.h"

#include <QTimer>
#include <algorithm>

namespace comp::ui {

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

void Playback::togglePlay() {
    transport_.toggle();
    if (transport_.playing()) {
        clock_.restart();
        lastFrameNs_ = 0;
        timer_->start();
    } else {
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
    emit timeChanged(transport_.time());
}

void Playback::tick() {
    const qint64 nowNs = clock_.nsecsElapsed();
    const double elapsed = static_cast<double>(nowNs - lastElapsedNs_) / 1e9;
    lastElapsedNs_ = nowNs;

    if (!transport_.advance(elapsed)) {
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

}  // namespace comp::ui
