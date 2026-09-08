#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ruby/core/Document.h"

namespace ruby::io {

// Every piece of media ever imported into the app, across every project and every
// session.
//
// This is deliberately NOT part of a Project. A project's media pool is scoped to that
// document and dies with it; this is scoped to the install. Putting it in the project
// file would mean the sixth project you open has no idea about the five before it, which
// is the entire thing the pool exists to fix.
struct PooledItem {
    std::string path;  // absolute; also the identity of the entry
    std::string name;
    core::MediaKind kind = core::MediaKind::Unknown;

    double duration = 0.0;
    std::int64_t bytes = 0;

    // Seconds since the Unix epoch, the first time this path was imported anywhere.
    //
    // Not recoverable after the fact: a file's own timestamps say when it was made or
    // touched, not when you first pulled it into Ruby. If it is not written at import
    // time it is gone, so it is written at import time.
    std::int64_t firstSeen = 0;
};

class MediaPool {
public:
    // Adds the item unless its path is already here. Importing one clip into six
    // projects should be one entry, not six. Returns true if it was new.
    bool add(const PooledItem& item);

    [[nodiscard]] const std::vector<PooledItem>& items() const noexcept { return items_; }
    [[nodiscard]] bool empty() const noexcept { return items_.empty(); }

    // Both are best-effort and never throw. A pool that fails to load is an empty pool,
    // not a failed launch: it is a convenience, and it must not be able to stop the app
    // from opening.
    void load(const std::string& file);
    bool save(const std::string& file) const;

private:
    std::vector<PooledItem> items_;
};

}  // namespace ruby::io
