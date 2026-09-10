#pragma once

#include <string>
#include <vector>

#include "ruby/core/Identity.h"

namespace ruby::io {

// Effect schemas, written to disk as the record of what has shipped.
//
// D1 says a parameter key is immortal and a schema version is bumped on any parameter
// change. Rules like that need something to check against, and the code cannot check
// against itself: the whole point is comparing what an effect declares TODAY with what it
// declared when someone's project was saved. That comparison needs yesterday's schema
// written down somewhere, and `schemas/effects/*.json` is where.
//
// The files are the authority for what shipped. The registry is the authority for what
// the app does now. A test compares them and fails when they disagree, which is the only
// moment anybody can still be told to bump a version and write a migration.
//
// Deliberately not generated at build time. A generated file cannot be reviewed in a
// diff, and "you changed this parameter's unit" is exactly the kind of line that has to
// show up in a diff.

[[nodiscard]] std::string schemaToJson(const core::EffectSchema& schema);

// Parses one back. Returns false and leaves `out` untouched on malformed input; `error`
// gets a reason when supplied.
[[nodiscard]] bool schemaFromJson(const std::string& text, core::EffectSchema& out,
                                  std::string* error = nullptr);

}  // namespace ruby::io
