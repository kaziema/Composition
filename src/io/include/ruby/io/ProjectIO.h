#pragma once

#include <string>
#include <vector>

#include "ruby/core/Document.h"

namespace ruby::io {

// Reading and writing `.rbypr` project files.
//
// Plain JSON, not a binary blob and not a zip. A project is references and numbers; it
// carries no assets, so a container buys nothing. The cautionary tale is AE's `.aep`:
// an opaque big-endian RIFF that nobody can inspect, diff, merge or recover, which is
// why there is a cottage industry of reverse-engineered parsers for it.
// JSON is greppable, diffable, and survives a bad byte with the damage visible.

// What went wrong, or what was quietly fixed up. A project that half-loads and says
// nothing is worse than one that refuses.
struct LoadReport {
    bool ok = false;
    std::string error;             // set when the file could not be read at all
    std::vector<std::string> notes;  // recoverable problems, in the order they happened

    [[nodiscard]] bool clean() const noexcept { return ok && notes.empty(); }
};

// The version stamped into every file we write. Bumped whenever the on-disk shape
// changes; the loader uses it to decide which migrations to run.
inline constexpr int kProjectSchema = 1;

[[nodiscard]] bool save(const core::Project& project, const std::string& path,
                        std::string* error = nullptr);

// Never throws and never half-populates: on failure `project` is left untouched.
[[nodiscard]] LoadReport load(core::Project& project, const std::string& path);

// Exposed for tests, and for anything that wants a project as a string.
[[nodiscard]] std::string toJson(const core::Project& project);
[[nodiscard]] LoadReport fromJson(core::Project& project, const std::string& text);

}  // namespace ruby::io
