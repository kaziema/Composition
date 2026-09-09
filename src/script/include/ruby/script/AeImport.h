#pragma once

#include <string>
#include <vector>

namespace ruby::script {

// Converts an After Effects expression into the Lua this app runs.
//
// This exists because of the one real cost of choosing Lua: AE expressions are JavaScript,
// and this audience pastes them from tutorials and Discord rather than writing them. See
// NOTEBOOK F5. Scalar one-liners are already valid Lua; what breaks is array literals,
// `var`, `//`, `Math.` and the boolean operators.
//
// Deliberately NOT a JavaScript to Lua translator. It handles the shapes that actually get
// pasted and REFUSES to guess at the rest, because a conversion that silently produces a
// wrong number is worse than one that says it cannot help. Anything it is unsure of comes
// back as a warning with the text left alone.
struct Conversion {
    std::string lua;

    // What was changed, so the result is not a black box.
    std::vector<std::string> notes;

    // What could not be handled. Non-empty means the output needs a human before it is
    // trusted, and the caller should say so rather than quietly accepting it.
    std::vector<std::string> warnings;

    [[nodiscard]] bool clean() const noexcept { return warnings.empty(); }
};

[[nodiscard]] Conversion convertFromAfterEffects(const std::string& source);

// Whether this looks like an AE expression rather than Lua already. Used to offer the
// conversion instead of performing it: converting something that was already Lua would be
// a fine way to break working expressions.
[[nodiscard]] bool looksLikeAfterEffects(const std::string& source);

}  // namespace ruby::script
