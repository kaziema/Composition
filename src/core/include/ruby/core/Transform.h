#pragma once

#include <cstdint>
#include <functional>

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

    // `a.then(b)` applies a first, then b: it maps p to b(a(p)). Reads left to right in
    // the order things actually happen, which is the order the parent chain walks.
    [[nodiscard]] Transform2D then(const Transform2D& outer) const noexcept;

    [[nodiscard]] double applyX(double x, double y) const noexcept;
    [[nodiscard]] double applyY(double x, double y) const noexcept;

    // The transform that undoes this one. Identity when this one is degenerate, which is
    // what a zero scale produces: a layer scaled to nothing has collapsed to a point and
    // there is no answer to "where did this pixel come from".
    //
    // Needed to go the other way through a parent chain. A parented layer's Position is
    // in its parent's space, so moving it a known distance in composition space means
    // asking the parent what that distance is worth to it.
    [[nodiscard]] Transform2D inverse() const noexcept;
};

// An axis-aligned box in composition pixels.
struct Bounds {
    double left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;

    [[nodiscard]] double width() const noexcept { return right - left; }
    [[nodiscard]] double height() const noexcept { return bottom - top; }
    [[nodiscard]] double centerX() const noexcept { return (left + right) / 2.0; }
    [[nodiscard]] double centerY() const noexcept { return (top + bottom) / 2.0; }
};

// How deep a parent chain may go before we stop walking it.
//
// This is not a style limit, it is a safety limit. Layer parenting is stored as an id, and
// nothing stops a file, a paste or a future scripting API from producing A parented to B
// parented to A. Walking that without a cap is an infinite loop inside the render path,
// which is a hung window with no error and nothing in the log.
inline constexpr int kMaxParentDepth = 32;

// How big a layer is, in composition pixels, before scale.
//
// Core cannot work this out: a footage layer's size comes from its decoded frames and a
// text layer's from a rasteriser, both of which live outside this module. So the caller
// supplies it, and every layer in a parent chain gets asked about itself rather than
// inheriting its child's dimensions.
struct LayerSize {
    double width = 0.0;
    double height = 0.0;
};
using SizeOf = std::function<LayerSize(const Layer&)>;

// A layer's own transform, in composition units (a fraction of the frame for position,
// percent for scale, degrees for rotation), before any parent is applied.
[[nodiscard]] Transform2D layerTransform(const Layer& layer, double seconds,
                                         const TimeContext& ctx, double compWidth,
                                         double compHeight, const LayerSize& size);

// The same, with every parent applied up the chain.
//
// Stops at kMaxParentDepth and stops on a repeat, so a cycle costs a bounded walk and the
// layer renders unparented rather than the app locking up. Silently: a broken parent link
// is a document problem, and the render path is the wrong place to complain about it.
//
// Each parent's anchor is computed against ITS OWN size, which is what `sizeOf` is for.
// Reusing the child's dimensions all the way up put every parent's pivot in the wrong
// place, and only visibly so once a parent had a non-centred anchor.
[[nodiscard]] Transform2D resolvedTransform(const Composition& comp, const Layer& layer,
                                            double seconds, const TimeContext& ctx,
                                            double compWidth, double compHeight,
                                            const SizeOf& sizeOf);

// Where a layer actually lands, as a box around it in composition pixels.
//
// The corners of the layer run through the same transform the compositor draws them with,
// including scale, rotation, anchor and every parent, and the box is drawn around wherever
// those four corners ended up. A rotated layer therefore reports the box that contains it,
// not its own tilted rectangle, which is what aligning to an edge means.
//
// Bounds at a moment in time, not for the layer as a whole: an animated layer is in a
// different place on every frame, so aligning it can only mean aligning it here.
[[nodiscard]] Bounds layerBounds(const Composition& comp, const Layer& layer,
                                 double seconds, const TimeContext& ctx, double compWidth,
                                 double compHeight, const SizeOf& sizeOf);

// Whether following this layer's parents leads back to itself, or runs deeper than the
// cap. The UI wants this to refuse a bad parenting BEFORE it is stored, which is the only
// place it can be reported to the person who caused it.
[[nodiscard]] bool hasParentCycle(const Composition& comp, LayerId layer) noexcept;

// Whether `candidate` may become `layer`'s parent. False when it is the layer itself, or
// when the layer is already somewhere above the candidate.
[[nodiscard]] bool canParentTo(const Composition& comp, LayerId layer,
                               LayerId candidate) noexcept;

}  // namespace ruby::core
