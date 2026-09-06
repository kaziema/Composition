#pragma once

#include <string>
#include <vector>

#include "comp/core/Document.h"

namespace comp::ui::demo {

// TEMPORARY SCAFFOLDING. Delete once the app can open a real project file.
//
// Builds the composition from the design handoff's main-editor screen, down to the
// exact keyframe times, so the timeline can be compared against the spec directly
// instead of against my memory of it.
// Footage layers use these paths in order, as many as are supplied.
void setMediaPaths(std::vector<std::string> paths);

[[nodiscard]] core::Project sampleProject();

}  // namespace comp::ui::demo
