#pragma once

#include <string>
#include <vector>

#include "ruby/core/Document.h"

namespace ruby::ui::demo {

// TEMPORARY SCAFFOLDING. Delete once the app can open a real project file.
//
// A reference composition with realistic layers and keyframe times, so the timeline
// has something to draw before the app can open a project file.
// Footage layers use these paths in order, as many as are supplied.
void setMediaPaths(std::vector<std::string> paths);

[[nodiscard]] core::Project sampleProject();

}  // namespace ruby::ui::demo
