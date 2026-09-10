#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ruby/core/Animation.h"

namespace ruby::core {

// Moving old content onto a newer effect.
//
// D1: each effect ships an ordered list of pure functions over the serialized parameter
// bag, not over live objects. Loading schema 2 into an effect now at schema 7 runs the
// chain. Kept forever, because content authored in 2026 has to open in 2036.
//
// "Not over live objects" is the load-bearing part. A migration works on a dictionary of
// what was in the file, so it can be called from a test with three keys and an assertion
// and no Document, no registry, no GPU, and no effect implementation. A migration you
// cannot test is one you find out about from a user whose project opened wrong.
//
// The other reason: a migration frequently needs to read a key that no longer exists.
// Renaming `blur` to `radius` means reading `blur` after the schema has forgotten it. A
// bag remembers whatever was written; a live parameter list built from today's schema
// does not.

// What one effect instance's parameters looked like on disk.
class ParamBag {
public:
    struct Entry {
        Value value;                        // the static value
        std::vector<Keyframe> keys;         // empty when not animated
        std::optional<std::string> expression;
    };

    [[nodiscard]] bool has(const std::string& key) const;

    // Null when absent. Migrations are expected to check: the whole point is that old
    // content is missing things.
    [[nodiscard]] const Entry* find(const std::string& key) const;
    [[nodiscard]] Entry* find(const std::string& key);

    void set(const std::string& key, Entry entry);
    void remove(const std::string& key);

    // Moves a key's whole history, keyframes and expression included. The common
    // migration by a wide margin, and the one people get wrong by copying only the
    // static value and silently dropping the animation.
    //
    // Does nothing when `from` is absent. Overwrites `to` when both exist, because the
    // new key is what the schema believes in.
    void rename(const std::string& from, const std::string& to);

    // Every value in the bag scaled by `factor`: static value, every keyframe, every
    // component. For a unit change that is a pure rescale, which is the only kind of unit
    // change allowed to happen inside a migration rather than a new key.
    void scale(const std::string& key, double factor);

    [[nodiscard]] const std::map<std::string, Entry>& entries() const { return entries_; }
    [[nodiscard]] std::size_t size() const { return entries_.size(); }

private:
    std::map<std::string, Entry> entries_;
};

// One step. `to_schema` is the version this step PRODUCES, so a step with to_schema 3
// takes content at 2 and leaves it at 3.
struct MigrationStep {
    int to_schema = 0;
    std::function<void(ParamBag&)> apply;
};

// Runs every step needed to take `from` up to `to`, in order.
//
// Steps may be declared in any order and gaps are allowed: an effect that went 1 -> 4 with
// nothing to do at 2 and 3 ships one step at 4. Returns the version actually reached,
// which is `to` unless a step was missing, and a missing step is not an error: it means
// nothing had to change.
int runMigrations(const std::vector<MigrationStep>& steps, int from, int to, ParamBag& bag);

}  // namespace ruby::core
