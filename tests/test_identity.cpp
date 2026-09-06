// Tests for decision D1 (effect and parameter identity).
// These make D1's rules executable: a violation fails the build instead of
// silently breaking every preset ever made.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "comp/core/Identity.h"

using namespace comp::core;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

bool mentions(const std::vector<SchemaProblem>& problems, const std::string& needle) {
    for (const auto& p : problems) {
        if (p.message.find(needle) != std::string::npos ||
            p.where.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

EffectSchema good_effect() {
    EffectSchema s;
    s.id = "core.blur.directional";
    s.schema = 1;
    s.display_name = "Directional Blur";
    s.params = {
        {"amount", "Amount", 0, ParamType::Float, SpatialUnit::PercentOfDiagonal,
         2.5, std::nullopt, 1},
        {"angle", "Angle", 1, ParamType::Float, SpatialUnit::Degrees, 0.0, std::nullopt, 1},
    };
    return s;
}

void a_valid_schema_has_no_problems() {
    check(validate(good_effect()).empty(), "a well-formed schema validates clean");
}

void effect_ids_must_be_namespaced() {
    EffectSchema s = good_effect();
    s.id = "blur";
    check(!validate(s).empty(), "single-segment effect id is rejected");

    s.id = "Core.Blur";
    check(!validate(s).empty(), "capitalised effect id is rejected");

    s.id = "com.vendor.thing";
    check(validate(s).empty(), "third-party namespace is accepted");
}

void parameter_keys_must_be_snake_case_and_unique() {
    EffectSchema s = good_effect();
    s.params[1].key = "Angle";
    check(mentions(validate(s), "lower_snake_case"), "capitalised param key is rejected");

    s = good_effect();
    s.params[1].key = "amount";
    check(mentions(validate(s), "duplicate"), "duplicate param key is rejected");
}

void retired_keys_can_never_be_reused() {
    EffectSchema s = good_effect();
    s.schema = 2;
    s.retired_keys = {"amount"};
    check(mentions(validate(s), "retired"), "reusing a retired key is rejected");
}

// The AE lesson: changing a default silently rewrites saved work unless the old
// default is pinned for pre-existing content.
void params_added_later_need_a_legacy_default() {
    EffectSchema s = good_effect();
    s.schema = 2;
    s.params.push_back({"falloff", "Falloff", 2, ParamType::Float,
                        SpatialUnit::Normalized, 0.5, std::nullopt, 2});
    check(mentions(validate(s), "legacy_default"),
          "param introduced after v1 without legacy_default is rejected");

    s.params.back().legacy_default = 0.0;
    check(validate(s).empty(), "same param with a legacy_default validates clean");
}

void ids_and_types_are_immortal() {
    const EffectSchema before = good_effect();

    EffectSchema after = before;
    after.id = "core.blur.direction";
    check(mentions(validate_against_previous(before, after), "immortal"),
          "renaming an effect id is caught");

    after = before;
    after.schema = 2;
    after.params[0].type = ParamType::Int;
    check(mentions(validate_against_previous(before, after), "type changed"),
          "retyping a parameter is caught");

    after = before;
    after.schema = 2;
    after.params[0].unit = SpatialUnit::Px;
    check(mentions(validate_against_previous(before, after), "unit changed"),
          "changing a parameter's unit is caught");
}

void removing_a_param_requires_retiring_its_key() {
    const EffectSchema before = good_effect();

    EffectSchema after = before;
    after.schema = 2;
    after.params.pop_back();
    check(mentions(validate_against_previous(before, after), "retired_keys"),
          "removing a param without retiring its key is caught");

    after.retired_keys = {"angle"};
    check(validate_against_previous(before, after).empty(),
          "removing a param and retiring its key is fine");
}

void changing_a_default_requires_pinning_the_old_one() {
    const EffectSchema before = good_effect();

    EffectSchema after = before;
    after.schema = 2;
    after.params[0].default_value = 4.0;
    check(mentions(validate_against_previous(before, after), "legacy_default"),
          "moving a default without pinning the old one is caught");

    after.params[0].legacy_default = 2.5;  // the old default
    check(validate_against_previous(before, after).empty(),
          "moving a default while pinning the old one is fine");
}

}  // namespace

int main() {
    a_valid_schema_has_no_problems();
    effect_ids_must_be_namespaced();
    parameter_keys_must_be_snake_case_and_unique();
    retired_keys_can_never_be_reused();
    params_added_later_need_a_legacy_default();
    ids_and_types_are_immortal();
    removing_a_param_requires_retiring_its_key();
    changing_a_default_requires_pinning_the_old_one();

    if (failures != 0) {
        std::fprintf(stderr, "\n%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("identity: all checks passed");
    return EXIT_SUCCESS;
}
