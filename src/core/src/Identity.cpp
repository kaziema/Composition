#include "comp/core/Identity.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace comp::core {
namespace {

bool is_lower_snake(std::string_view s) noexcept {
    if (s.empty()) return false;
    if (s.front() == '_' || s.back() == '_') return false;
    for (const char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

// "core.blur.directional" / "com.vendor.thing"
bool is_valid_effect_id(std::string_view s) noexcept {
    if (s.empty()) return false;
    std::size_t start = 0;
    int segments = 0;
    while (start <= s.size()) {
        const std::size_t dot = s.find('.', start);
        const std::string_view seg =
            s.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
        if (!is_lower_snake(seg)) return false;
        ++segments;
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
    return segments >= 2;
}

const char* type_name(ParamType t) noexcept {
    switch (t) {
        case ParamType::Float:  return "Float";
        case ParamType::Int:    return "Int";
        case ParamType::Bool:   return "Bool";
        case ParamType::Color:  return "Color";
        case ParamType::Point2: return "Point2";
        case ParamType::Enum:   return "Enum";
        case ParamType::Text:   return "Text";
        case ParamType::Curve:  return "Curve";
    }
    return "?";
}

}  // namespace

const ParamSpec* EffectSchema::find(std::string_view key) const noexcept {
    const auto it = std::find_if(params.begin(), params.end(),
                                 [key](const ParamSpec& p) { return p.key == key; });
    return it == params.end() ? nullptr : &*it;
}

bool EffectSchema::is_retired(std::string_view key) const noexcept {
    return std::find(retired_keys.begin(), retired_keys.end(), key) != retired_keys.end();
}

std::vector<SchemaProblem> validate(const EffectSchema& s) {
    std::vector<SchemaProblem> out;
    const auto add = [&out](std::string where, std::string msg) {
        out.push_back({std::move(where), std::move(msg)});
    };

    if (!is_valid_effect_id(s.id)) {
        add(s.id.empty() ? "<empty>" : s.id,
            "effect id must be lower_snake segments separated by dots, at least two "
            "segments (e.g. \"core.blur.directional\")");
    }
    if (s.schema < 1) {
        add(s.id, "schema version must be >= 1");
    }
    if (s.display_name.empty()) {
        add(s.id, "display_name is empty");
    }

    std::unordered_set<std::string> seen;
    for (const ParamSpec& p : s.params) {
        const std::string where = s.id + "." + p.key;

        if (!is_lower_snake(p.key)) {
            add(where, "parameter key must be lower_snake_case and non-empty");
        }
        if (!seen.insert(p.key).second) {
            add(where, "duplicate parameter key");
        }
        if (s.is_retired(p.key)) {
            add(where, "key is in retired_keys and can never be reused");
        }
        if (p.label.empty()) {
            add(where, "label is empty");
        }
        if (p.introduced_in_schema < 1 || p.introduced_in_schema > s.schema) {
            add(where, "introduced_in_schema must be in [1, schema]");
        }
        // A parameter added after v1 is loaded into older content, so it needs a
        // legacy_default or old presets silently adopt the new default.
        if (p.introduced_in_schema > 1 && !p.legacy_default.has_value()) {
            add(where,
                "parameter introduced after schema 1 has no legacy_default; content "
                "authored before it existed will silently adopt default_value");
        }
    }

    std::unordered_set<std::string> retired_seen;
    for (const std::string& k : s.retired_keys) {
        if (!is_lower_snake(k)) {
            add(s.id + "." + k, "retired key is not a valid key");
        }
        if (!retired_seen.insert(k).second) {
            add(s.id + "." + k, "duplicate entry in retired_keys");
        }
    }

    return out;
}

std::vector<SchemaProblem> validate_against_previous(const EffectSchema& previous,
                                                     const EffectSchema& current) {
    std::vector<SchemaProblem> out;
    const auto add = [&out](std::string where, std::string msg) {
        out.push_back({std::move(where), std::move(msg)});
    };

    if (previous.id != current.id) {
        add(current.id, "effect id changed from \"" + previous.id + "\"; ids are immortal");
    }
    if (current.schema < previous.schema) {
        add(current.id, "schema version went backwards");
    }

    std::unordered_map<std::string, const ParamSpec*> prev_params;
    for (const ParamSpec& p : previous.params) prev_params.emplace(p.key, &p);

    for (const ParamSpec& now : current.params) {
        const auto it = prev_params.find(now.key);
        if (it == prev_params.end()) {
            if (now.introduced_in_schema <= previous.schema) {
                add(current.id + "." + now.key,
                    "new parameter claims introduced_in_schema <= previous schema");
            }
            continue;
        }
        const ParamSpec& before = *it->second;

        if (before.type != now.type) {
            add(current.id + "." + now.key,
                std::string("type changed from ") + type_name(before.type) + " to " +
                    type_name(now.type) + "; types are immortal, add a new key instead");
        }
        if (before.unit != now.unit) {
            add(current.id + "." + now.key,
                "unit changed; this silently reinterprets every stored value, add a new "
                "key and write a migration instead");
        }
        // Changing default_value is allowed, but only with a legacy_default pinning
        // what old content should keep using.
        const bool default_moved = before.default_value != now.default_value;
        const bool pinned = now.legacy_default.has_value() &&
                            now.legacy_default.value() == before.default_value;
        if (default_moved && !pinned) {
            add(current.id + "." + now.key,
                "default_value changed without a legacy_default pinning the old default; "
                "existing content will silently change");
        }
    }

    for (const auto& [key, before] : prev_params) {
        (void)before;
        const bool still_present = current.find(key) != nullptr;
        if (!still_present && !current.is_retired(key)) {
            add(current.id + "." + key,
                "parameter removed but not added to retired_keys; the key could be "
                "reused later and corrupt old presets");
        }
    }

    return out;
}

}  // namespace comp::core
