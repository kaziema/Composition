#pragma once

#include <cstdint>
#include <string>

#include "ruby/core/Document.h"

namespace ruby::core {

// The seam between the document and whatever runs expressions.
//
// Core cannot call Lua: it has no dependencies and it is going to keep it that way, both
// because that is what makes it testable in a tenth of a second and because a document
// model that needs an interpreter to be read is a document model you cannot write a
// migration tool for.
//
// So core declares what it needs and the script module supplies it. Anything that cannot
// find a host evaluates keyframes, which is exactly what happened before expressions
// existed. That is the correct fallback and it is why this is an interface rather than a
// hard dependency: `test_document` still runs with no interpreter anywhere.
class ExpressionHost {
public:
    virtual ~ExpressionHost() = default;

    // Returns false to mean "use the keyframed value". An expression that fails is not an
    // error the renderer should propagate: a frame that draws with the wrong number is
    // recoverable, a frame that does not draw at all is not.
    // The whole property is passed, not just its source text, because `loopOut` and
    // anything like it needs the keyframes. A host that only saw a string could never
    // implement the half of AE's expression language that reads the animation it is
    // attached to.
    [[nodiscard]] virtual bool evaluate(const Property& prop, double seconds,
                                        const TimeContext& ctx, const Value& fallback,
                                        std::uint64_t seed, Value& out) = 0;
};

// Thread local, deliberately.
//
// A Lua state belongs to one thread, so when rendering goes parallel each thread installs
// its own host. A single shared one would need a lock in the hot path of every animated
// property, which is the one place a lock cannot go.
void setExpressionHost(ExpressionHost* host);
[[nodiscard]] ExpressionHost* expressionHost() noexcept;

// A property's value, running its expression if it has one and there is a host to run it.
//
// The layer is here for the seed, and only for the seed. Two layers carrying the same
// expression have to wiggle differently, and the same layer has to wiggle identically on
// every render, which means the seed is a function of the layer's identity and the
// property's name and nothing else.
[[nodiscard]] Value evaluate(const Layer& layer, const Property& prop, double seconds,
                             const TimeContext& ctx);

// The seed for one property on one layer. Exposed because it is worth testing directly:
// if this stops being stable, every wiggle in every project changes at once.
[[nodiscard]] std::uint64_t expressionSeed(LayerId layer, std::string_view key) noexcept;

}  // namespace ruby::core
