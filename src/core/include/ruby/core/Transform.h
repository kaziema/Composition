#pragma once

#include <cstdint>

#include "ruby/core/Document.h"

namespace ruby::core {

// A 2D affine transform, stored as the six meaningful values of a 3x3.
//
//     | a  c  tx |
//     | b  d  ty |
//     | 0  0  1  |
//
// Six rather than a 4x4 because that is all a 2D layer needs, and because the compositor
// has to widen it to a mat4x4 for the shader anyway. Keeping the narrow form here means
// the parent chain composes ten multiplies instead of sixty-four, and means this header
// stays something you can read.
struct Transform2D {
    double a = 1.0, b = 0.0;
    double c = 0.0, d = 1.0;
    double tx = 0.0, ty = 0.0;

    [[nodiscard]] static Transform2D identity() noexcept { return {}; }
    [[nodiscard]] static Transform2D translate(double x, double y) noexcept;
    [[nodiscard]] static Transform2D scale(double x, double y) noexcept;
    [[nodiscard]] static Transform2D rotate(double degrees) noexcept;

    // this applied AFTER inner. Reads left to right the way the parent chain does:
    // parent.then(child) puts the child inside the parent's space.
    [[nodiscard]] Transform2D then(const Transform2D& outer) const noexcept;

    [[nodiscard]] double applyX(double x, double y) const noexcept;
    [[nodiscard]] double applyY(double x, double y) const noexcept;
};

// How deep a parent chain may go before we stop walking it.
//
// This is not a style limit, it is a safety limit. Layer parenting is stored as an id, and
// nothing stops a file, a paste or a future scripting API from producing A parented to B
// parented to A. Walking that without a cap is an infinite loop inside the render path,
// which is a hung window with no error and nothing in the log.
inline constexpr int kMaxParentDepth = 32;

// A layer's own transform, in composition units (a fraction of the frame for position,
// percent for scale, degrees for rotation), before any parent is applied.
//
// `frameWidth`/`frameHeight` are the layer's own size in composition pixels, needed
// because the anchor point is expressed relative to the layer, not to the frame.
[[nodiscard]] Transform2D layerTransform(const Layer& layer, double seconds,
                                         const TimeContext& ctx, double compWidth,
                                         double compHeight, double layerWidth,
                                         double layerHeight);

// The same, with every parent applied up the chain.
//
// Stops at kMaxParentDepth and stops on a repeat, so a cycle costs a bounded walk and the
// layer renders unparented rather than the app locking up. Silently: a broken parent link
// is a document problem, and the render path is the wrong place to complain about it.
[[nodiscard]] Transform2D resolvedTransform(const Composition& comp, const Layer& layer,
                                            double seconds, const TimeContext& ctx,
                                            double compWidth, double compHeight,
                                            double layerWidth, double layerHeight);

// Whether following this layer's parents leads back to itself, or runs deeper than the
// cap. The UI wants this to refuse a bad parenting BEFORE it is stored, which is the only
// place it can be reported to the person who caused it.
[[nodiscard]] bool hasParentCycle(const Composition& comp, LayerId layer) noexcept;

// Whether `candidate` may become `layer`'s parent. False when it is the layer itself, or
// when the layer is already somewhere above the candidate.
[[nodiscard]] bool canParentTo(const Composition& comp, LayerId layer,
                               LayerId candidate) noexcept;

}  // namespace ruby::core
