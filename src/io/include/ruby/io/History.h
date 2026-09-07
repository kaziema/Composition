#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ruby/core/Document.h"

namespace ruby::io {

// Undo and redo, by snapshotting the document.
//
// Not a command pattern. Every command-pattern bug is some `undo()` that does not quite
// invert its `do()`, and there is no way to test for that class of mistake except by
// finding it. A snapshot cannot be wrong about what the document used to be. Task 4
// already gave us a lossless serialiser, so this costs one call per edit and nothing
// per operation.
//
// The cost is memory, and it is small: a project is references and numbers, so a
// snapshot is tens of kilobytes of text, not the media.
//
// Selection and scroll position are deliberately NOT captured. Undoing a value change
// should not jump you to a different layer.
class History {
public:
    explicit History(std::size_t limit = 200);

    // Records the state to return to. Call BEFORE mutating, with a label describing what
    // is about to happen ("Set Opacity", "Add Layer") so the menu can say "Undo Add Layer".
    void record(const core::Project& before, std::string label);

    // A drag fires hundreds of changes a second and must be one undo step, not four
    // hundred. Open before the gesture, close after; records in between coalesce into
    // the first one.
    void beginGesture(const core::Project& before, std::string label);
    void endGesture();
    [[nodiscard]] bool inGesture() const noexcept { return gestureDepth_ > 0; }

    [[nodiscard]] bool canUndo() const noexcept { return !past_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !future_.empty(); }

    // What the next undo or redo would do, for the menu. Empty when unavailable.
    [[nodiscard]] std::string undoLabel() const;
    [[nodiscard]] std::string redoLabel() const;

    // `current` is read to build the opposite stack's entry, then replaced. False when
    // there was nothing to do, in which case `current` is untouched.
    bool undo(core::Project& current);
    bool redo(core::Project& current);

    void clear();

private:
    struct Entry {
        std::string json;
        std::string label;
    };

    std::vector<Entry> past_;
    std::vector<Entry> future_;
    std::size_t limit_;
    int gestureDepth_ = 0;
};

}  // namespace ruby::io
