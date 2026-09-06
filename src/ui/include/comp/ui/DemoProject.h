#pragma once

#include "comp/core/Document.h"

namespace comp::ui::demo {

// TEMPORARY SCAFFOLDING. Delete once the app can open a real project file.
//
// Builds the composition from the design handoff's main-editor screen, down to the
// exact keyframe times, so the timeline can be compared against the spec directly
// instead of against my memory of it.
[[nodiscard]] core::Project sampleProject();

}  // namespace comp::ui::demo
