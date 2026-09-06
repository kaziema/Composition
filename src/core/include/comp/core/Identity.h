#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "comp/core/Units.h"

namespace comp::core {

// --- Effect and parameter identity (decision D1) -----------------------------
//
// Adapted from After Effects' two-list split: display order is separate from
// persistent identity. AE uses a "parameter array index" (freely rearrangeable)
// and a "disk ID" (written to saved projects, never reordered, only appended to).
// We keep that split but use string keys instead of integers, because they are
// readable and diffable.
//
// Evidence this is load-bearing: in a real AE pack, Sapphire's S_BlurMoCurves
// serializes parameters as S_BlurMoCurves-0000 through S_BlurMoCurves-0542 for
// roughly 50 visible controls. Those gaps are a decade of adding and retiring
// parameters while old presets kept working.
//
// THE RULES, non-negotiable once shipped:
//   - A key is immortal. Never rename, never retype, never change its meaning.
//   - Deleting a parameter retires its key permanently (see retired_keys).
//     A retired key can never be reused.
//   - Adding is append-only, with a default.
//   - Two defaults: `default_value` for new instances, `legacy_default` for
//     content authored before the parameter existed or before the default
//     changed. Without this, improving a default silently rewrites saved work.
//     (AE's PF_ParamFlag_USE_VALUE_FOR_OLD_PROJECTS, learned the hard way.)
//   - Changing a parameter's meaning is forbidden. Add a new key, write a
//     migration.

enum class ParamType {
    Float,
    Int,
    Bool,
    Color,
    Point2,
    Enum,
    Text,
    Curve,
};

struct ParamSpec {
    std::string key;                       // immortal, snake_case
    std::string label;                     // free to change, localizable
    int order = 0;                         // display order, free to change
    ParamType type = ParamType::Float;
    SpatialUnit unit = SpatialUnit::Normalized;

    double default_value = 0.0;            // for new instances
    std::optional<double> legacy_default;  // for content predating this param

    int introduced_in_schema = 1;
};

struct EffectSchema {
    std::string id;            // "core.blur.directional" — immortal
    int schema = 1;            // bumped on any parameter change
    std::string display_name;  // free to change

    std::vector<ParamSpec> params;
    std::vector<std::string> retired_keys;  // permanently unusable

    [[nodiscard]] const ParamSpec* find(std::string_view key) const noexcept;
    [[nodiscard]] bool is_retired(std::string_view key) const noexcept;
};

// --- Schema validation -------------------------------------------------------
//
// Makes D1's rules executable rather than aspirational. Every registered effect
// runs through this in tests, so a rule violation fails the build instead of
// silently breaking every preset ever made.

struct SchemaProblem {
    std::string where;    // effect id, or "<effect id>.<param key>"
    std::string message;
};

[[nodiscard]] std::vector<SchemaProblem> validate(const EffectSchema& s);

// Enforced separately because it needs both versions: compares a newly edited
// schema against the last shipped one and reports anything that would break
// existing presets (renamed keys, retyped params, un-retired keys, silently
// changed defaults).
[[nodiscard]] std::vector<SchemaProblem> validate_against_previous(
    const EffectSchema& previous, const EffectSchema& current);

}  // namespace comp::core
