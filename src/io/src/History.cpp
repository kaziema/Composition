#include "ruby/io/History.h"

#include <utility>

#include "ruby/io/ProjectIO.h"

namespace ruby::io {

History::History(std::size_t limit) : limit_(limit == 0 ? 1 : limit) {}

void History::record(const core::Project& before, std::string label) {
    // Inside a gesture the opening snapshot is the one we want to return to, so every
    // change after it is dropped rather than stacked.
    if (gestureDepth_ > 0) {
        return;
    }
    past_.push_back({toJson(before), std::move(label)});

    // Anything redone was a different future. Keeping it would let you redo your way
    // into a document that never existed.
    future_.clear();

    if (past_.size() > limit_) {
        past_.erase(past_.begin());
    }
}

void History::beginGesture(const core::Project& before, std::string label) {
    // Nested begins are one gesture. A drag that triggers another recorded edit should
    // not split into two undo steps.
    //
    // The snapshot has to be taken before the depth goes up, or record()'s own
    // "skip while in a gesture" guard swallows the one entry we actually need.
    if (gestureDepth_ == 0) {
        record(before, std::move(label));
    }
    ++gestureDepth_;
}

void History::endGesture() {
    if (gestureDepth_ > 0) {
        --gestureDepth_;
    }
}

std::string History::undoLabel() const {
    return past_.empty() ? std::string{} : past_.back().label;
}

std::string History::redoLabel() const {
    return future_.empty() ? std::string{} : future_.back().label;
}

bool History::undo(core::Project& current) {
    if (past_.empty()) {
        return false;
    }
    Entry entry = std::move(past_.back());
    past_.pop_back();

    core::Project restored;
    if (!fromJson(restored, entry.json).ok) {
        // A snapshot we wrote failing to load means the serialiser is broken. Dropping
        // the entry is better than corrupting the live document with a partial parse.
        return false;
    }

    future_.push_back({toJson(current), entry.label});
    current = std::move(restored);
    return true;
}

bool History::redo(core::Project& current) {
    if (future_.empty()) {
        return false;
    }
    Entry entry = std::move(future_.back());
    future_.pop_back();

    core::Project restored;
    if (!fromJson(restored, entry.json).ok) {
        return false;
    }

    past_.push_back({toJson(current), entry.label});
    current = std::move(restored);
    return true;
}

void History::clear() {
    past_.clear();
    future_.clear();
    gestureDepth_ = 0;
}

}  // namespace ruby::io
