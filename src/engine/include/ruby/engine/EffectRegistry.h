#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ruby/core/Document.h"
#include "ruby/core/Identity.h"

namespace ruby::engine {

// One built-in effect: its schema plus the shader that implements it.
//
// Parameters are packed into the uniform block in schema order, one vec4 each, so the
// shader indexes `u.params[n]` and no per-effect C++ is needed to marshal anything. Add
// a parameter to the schema and the plumbing follows.
struct EffectDef {
    core::EffectSchema schema;
    std::string shader;  // WGSL, fullscreen pass

    static constexpr int kMaxParams = 8;
};

// The submenu an effect belongs in, taken from the middle segment of its id:
// "core.color.grade" is Color, "core.blur.directional" is Blur.
//
// Derived rather than stored. The id is already immortal and already says this, and a
// separate category field would be a second source of truth that could disagree with it.
[[nodiscard]] std::string effectCategory(std::string_view id);

// The built-in effect library. Every effect ships with the app; that is Pillar 1, and it
// is why this is a fixed table rather than a plugin loader.
class EffectRegistry {
public:
    [[nodiscard]] static const EffectRegistry& instance();

    [[nodiscard]] const EffectDef* find(std::string_view id) const noexcept;
    [[nodiscard]] const std::vector<EffectDef>& all() const noexcept { return effects_; }

    // A ready-to-use instance with every parameter at its default.
    [[nodiscard]] core::EffectInstance instantiate(std::string_view id) const;

    // Puts the schema-owned parts of a loaded instance back: ranges and group names.
    // Project files store the values a user chose, not the limits the effect declares,
    // so a freshly loaded effect arrives with default ranges until this runs.
    void adoptSchema(core::EffectInstance& instance) const;

private:
    EffectRegistry();

    std::vector<EffectDef> effects_;
};

}  // namespace ruby::engine
