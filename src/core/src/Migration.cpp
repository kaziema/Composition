#include "ruby/core/Migration.h"

#include <algorithm>

namespace ruby::core {

bool ParamBag::has(const std::string& key) const { return entries_.count(key) != 0; }

const ParamBag::Entry* ParamBag::find(const std::string& key) const {
    const auto it = entries_.find(key);
    return it == entries_.end() ? nullptr : &it->second;
}

ParamBag::Entry* ParamBag::find(const std::string& key) {
    const auto it = entries_.find(key);
    return it == entries_.end() ? nullptr : &it->second;
}

void ParamBag::set(const std::string& key, Entry entry) {
    entries_[key] = std::move(entry);
}

void ParamBag::remove(const std::string& key) { entries_.erase(key); }

void ParamBag::rename(const std::string& from, const std::string& to) {
    const auto it = entries_.find(from);
    if (it == entries_.end() || from == to) {
        return;
    }
    entries_[to] = std::move(it->second);
    entries_.erase(from);
}

void ParamBag::scale(const std::string& key, double factor) {
    Entry* e = find(key);
    if (e == nullptr) {
        return;
    }
    const auto scaleValue = [factor](Value& v) {
        for (int i = 0; i < v.count; ++i) {
            v.c[static_cast<std::size_t>(i)] *= factor;
        }
    };
    scaleValue(e->value);
    for (Keyframe& k : e->keys) {
        scaleValue(k.value);
    }
    // An expression is left alone deliberately. Rescaling `wiggle(3, 40)` would mean
    // parsing and rewriting someone's code, and getting that wrong is worse than leaving
    // an expression that needs a human to look at it.
}

int runMigrations(const std::vector<MigrationStep>& steps, int from, int to,
                  ParamBag& bag) {
    if (from >= to) {
        return from;
    }

    // Sorted rather than trusted, so a step appended out of order at the bottom of a list
    // still runs in the right place. Declaration order is a comment; to_schema is the fact.
    std::vector<const MigrationStep*> ordered;
    ordered.reserve(steps.size());
    for (const MigrationStep& s : steps) {
        if (s.to_schema > from && s.to_schema <= to && s.apply) {
            ordered.push_back(&s);
        }
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const MigrationStep* a, const MigrationStep* b) {
                  return a->to_schema < b->to_schema;
              });

    for (const MigrationStep* s : ordered) {
        s->apply(bag);
    }
    // `to` regardless of whether every version had a step. A version with nothing to do is
    // the normal case: most bumps add a parameter, and adding is handled by defaults
    // rather than by a migration.
    return to;
}

}  // namespace ruby::core
