#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ruby/core/Document.h"
#include "ruby/core/Identity.h"
#include "ruby/core/Migration.h"

namespace ruby::engine {

// One built-in effect: its schema plus the shader that implements it.
//
// Parameters are packed into the uniform block in schema order, one vec4 each, so the
// shader indexes `u.params[n]` and no per-effect C++ is needed to marshal anything. Add
// a parameter to the schema and the plumbing follows.
struct EffectDef {
    core::EffectSchema schema;
    std::string shader;  // WGSL, fullscreen pass

    // Ordered steps taking old content forward, one per version that needed one. Kept
    // forever: a project saved today has to open in ten years, and the only thing that
    // makes that true is that nobody ever deleted one of these.
    //
    // Empty for every effect right now, because every effect is at schema 1 and there is
    // no earlier version of anything to move. The harness exists before it is needed on
    // purpose: it is near-free now and impossible to retrofit once content is in the wild.
    std::vector<core::MigrationStep> migrations;

    static constexpr int kMaxParams = 8;
};

// What happened to one instance on the way in. Migrations are never silent: a project
// that quietly reinterprets your work is worse than one that says what it did.
struct EffectRegistryMigrationReport {
    bool ok = true;  // false only when the content cannot be used as is
    int from_schema = 0;
    int to_schema = 0;
    std::vector<std::string> notes;

    [[nodiscard]] bool changed() const noexcept { return from_schema != to_schema; }
};

// The same work, against a definition supplied by the caller.
//
// Exists because the rule this harness is really for — a parameter added later loading
// into older content takes its legacy_default, not today's default — cannot be exercised
// against the built-ins, which are all at schema 1 and have never been through a version.
// A test that cannot reach the most important branch is a test that will discover it is
// broken from a user.
[[nodiscard]] EffectRegistryMigrationReport migrateAgainst(const EffectDef& def,
                                                           core::EffectInstance& instance);

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

    using MigrationReport = EffectRegistryMigrationReport;

    // Brings a loaded instance up to the current schema.
    //
    // Four things, in order, and the order matters:
    //   1. Run the effect's migration chain over a bag of what was in the file.
    //   2. Fill in parameters the content does not have, choosing default_value or
    //      legacy_default by whether the parameter existed when the content was authored.
    //   3. Drop keys that are retired or that no schema has ever heard of.
    //   4. Re-adopt ranges and groups, and stamp the new version.
    [[nodiscard]] MigrationReport migrate(core::EffectInstance& instance) const;

    // Puts the schema-owned parts of a loaded instance back: ranges and group names.
    // Project files store the values a user chose, not the limits the effect declares,
    // so a freshly loaded effect arrives with default ranges until this runs.
    void adoptSchema(core::EffectInstance& instance) const;

private:
    EffectRegistry();

    std::vector<EffectDef> effects_;
};

}  // namespace ruby::engine
