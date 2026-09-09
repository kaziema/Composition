#include "ruby/core/Expressions.h"

namespace ruby::core {
namespace {

// Thread local so each render thread can own its own interpreter without a lock. See the
// header: this is the one place a lock cannot go.
thread_local ExpressionHost* g_host = nullptr;

}  // namespace

void setExpressionHost(ExpressionHost* host) { g_host = host; }
ExpressionHost* expressionHost() noexcept { return g_host; }

std::uint64_t expressionSeed(LayerId layer, std::string_view key) noexcept {
    // FNV-1a over the property name, then mixed with the layer id.
    //
    // Both halves matter and for different reasons. Without the layer id, every layer
    // carrying the same expression wiggles in perfect sympathy, which looks like a bug and
    // is the first thing anyone notices. Without the key, Position and Scale on one layer
    // wiggle together, which looks subtly wrong in a way nobody can name.
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char c : key) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    hash ^= layer + 0x9E3779B97F4A7C15ULL;

    // splitmix64 finalizer, so consecutive layer ids do not produce related seeds.
    hash = (hash ^ (hash >> 30)) * 0xBF58476D1CE4E5B9ULL;
    hash = (hash ^ (hash >> 27)) * 0x94D049BB133111EBULL;
    return hash ^ (hash >> 31);
}

Value evaluate(const Layer& layer, const Property& prop, double seconds,
               const TimeContext& ctx) {
    const Value keyframed = prop.evaluate(seconds, ctx);
    // An absent expression and an empty one mean the same thing: nothing to run.
    if (!prop.expression.has_value() || prop.expression->empty()) {
        return keyframed;
    }
    ExpressionHost* host = expressionHost();
    if (host == nullptr) {
        // No interpreter installed. Every test that does not care about expressions runs
        // this path, and so does any tool that reads a project without rendering it.
        return keyframed;
    }

    Value out;
    if (!host->evaluate(prop, seconds, ctx, keyframed,
                        expressionSeed(layer.id, prop.key), out)) {
        return keyframed;
    }

    // An expression that returns the wrong shape keeps the keyframed value's shape rather
    // than changing what the property IS. A scalar arriving on a vec2 Position would
    // otherwise silently move the layer to the origin in y.
    if (out.count != keyframed.count) {
        if (out.count == 1 && keyframed.count > 1) {
            // A single number broadcast across a vector is the one widening worth doing:
            // `value[1] * 2` on Position is a real thing people write.
            Value widened = keyframed;
            for (int i = 0; i < widened.count; ++i) {
                widened.c[static_cast<std::size_t>(i)] = out.c[0];
            }
            return widened;
        }
        return keyframed;
    }
    return out;
}

}  // namespace ruby::core
