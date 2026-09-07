#pragma once

#include <QString>

#include "ruby/core/Animation.h"
#include "ruby/core/Units.h"

namespace ruby::ui {

// Shared display formatting. Both the timeline and the inspector show the same values,
// so they read from one implementation rather than two copies that drift.

// MM:SS:FF at the composition's frame rate.
[[nodiscard]] QString formatTimecode(double seconds, double fps);

// A property's value at a given time, with its unit's suffix.
[[nodiscard]] QString formatPropertyValue(const core::Property& prop, double seconds,
                                          const core::TimeContext& ctx);

}  // namespace ruby::ui
