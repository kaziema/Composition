#include "ruby/core/Transform.h"

#include "ruby/core/Expressions.h"

#include <algorithm>
#include <cmath>

namespace ruby::core {
namespace {

// A property's component at a time, or a fallback when the property is absent. Layers
// built before a property existed simply do not have it, and that has to be survivable.
double componentOr(const Layer& layer, std::string_view key, int index, double fallback,
                   double seconds, const TimeContext& ctx) {
    const Property* prop = layer.find(key);
    if (prop == nullptr) {
        return fallback;
    }
    // Through core::evaluate, not Property::evaluate, so an expression on Position or
    // Rotation actually runs. Falls straight back to keyframes when no interpreter is
    // installed, which is every test that does not care.
    const Value v = evaluate(layer, *prop, seconds, ctx);
    const double got = (index < v.count) ? v.c[static_cast<std::size_t>(index)] : fallback;

    // A non-finite value here would poison the whole matrix, and a NaN matrix reaching the
    // GPU makes a layer silently vanish with nothing to diagnose. It can arrive from a
    // corrupt file today and from an expression the moment scripting lands, so it is
    // caught at the one point every transform value passes through.
    return std::isfinite(got) ? got : fallback;
}

}  // namespace

Transform2D Transform2D::translate(double x, double y) noexcept {
    Transform2D t;
    t.tx = x;
    t.ty = y;
    return t;
}

Transform2D Transform2D::scale(double x, double y) noexcept {
    Transform2D t;
    t.a = x;
    t.d = y;
    return t;
}

Transform2D Transform2D::rotate(double degrees) noexcept {
    const double r = degrees * 3.14159265358979323846 / 180.0;
    const double s = std::sin(r);
    const double c = std::cos(r);
    Transform2D t;
    t.a = c;
    t.b = s;
    t.c = -s;
    t.d = c;
    return t;
}

Transform2D Transform2D::then(const Transform2D& outer) const noexcept {
    Transform2D out;
    out.a = a * outer.a + b * outer.c;
    out.b = a * outer.b + b * outer.d;
    out.c = c * outer.a + d * outer.c;
    out.d = c * outer.b + d * outer.d;
    out.tx = tx * outer.a + ty * outer.c + outer.tx;
    out.ty = tx * outer.b + ty * outer.d + outer.ty;
    return out;
}

double Transform2D::applyX(double x, double y) const noexcept { return a * x + c * y + tx; }
double Transform2D::applyY(double x, double y) const noexcept { return b * x + d * y + ty; }

Transform2D Transform2D::inverse() const noexcept {
    const double det = a * d - b * c;
    if (std::fabs(det) < 1e-12) {
        return identity();  // collapsed to a point or a line; nothing to undo
    }
    const double ia = d / det;
    const double ib = -b / det;
    const double ic = -c / det;
    const double id = a / det;
    return Transform2D{ia, ib, ic, id, -(ia * tx + ic * ty), -(ib * tx + id * ty)};
}

Bounds layerBounds(const Composition& comp, const Layer& layer, double seconds,
                   const TimeContext& ctx, double compWidth, double compHeight,
                   const SizeOf& sizeOf) {
    const LayerSize size = sizeOf ? sizeOf(layer) : LayerSize{};

    // The same two steps the compositor uses, in the same order, because a box that
    // disagrees with what is on screen is worse than no box at all. Unit square, centred,
    // scaled to the layer, then through the layer's full transform.
    const Transform2D unitToLayer =
        Transform2D::translate(-0.5, -0.5).then(Transform2D::scale(size.width, size.height));
    const Transform2D toComp =
        unitToLayer.then(resolvedTransform(comp, layer, seconds, ctx, compWidth,
                                           compHeight, sizeOf));

    const double corners[4][2] = {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}};
    Bounds out{};
    for (int i = 0; i < 4; ++i) {
        const double x = toComp.applyX(corners[i][0], corners[i][1]);
        const double y = toComp.applyY(corners[i][0], corners[i][1]);
        if (i == 0) {
            out = Bounds{x, y, x, y};
            continue;
        }
        out.left = std::min(out.left, x);
        out.top = std::min(out.top, y);
        out.right = std::max(out.right, x);
        out.bottom = std::max(out.bottom, y);
    }
    return out;
}

Transform2D layerTransform(const Layer& layer, double seconds, const TimeContext& ctx,
                           double compWidth, double compHeight, const LayerSize& size) {
    // Position and anchor point are percentages, which is what makes a preset built on a
    // vertical composition land correctly on a landscape one.
    const double px = componentOr(layer, "position", 0, 50.0, seconds, ctx);
    const double py = componentOr(layer, "position", 1, 50.0, seconds, ctx);
    const double ax = componentOr(layer, "anchor_point", 0, 0.0, seconds, ctx);
    const double ay = componentOr(layer, "anchor_point", 1, 0.0, seconds, ctx);
    const double sx = componentOr(layer, "scale", 0, 100.0, seconds, ctx);
    const double sy = componentOr(layer, "scale", 1, 100.0, seconds, ctx);
    const double rot = componentOr(layer, "rotation", 0, 0.0, seconds, ctx);

    // The anchor is a percentage of the LAYER, not of the frame: it is the point on the
    // layer that sits at Position and that rotation and scale pivot around. 0,0 is the
    // centre so an untouched layer rotates about itself, which is what people expect and
    // what the default transform already stores.
    const double anchorX = ax / 100.0 * size.width;
    const double anchorY = ay / 100.0 * size.height;

    // Order matters and this is the order: move the anchor to the origin, scale, rotate,
    // then move to the position. Rotating before centring on the anchor makes a layer
    // swing around a point it is not on, which is the classic wrong-looking result.
    return Transform2D::translate(-anchorX, -anchorY)
        .then(Transform2D::scale(sx / 100.0, sy / 100.0))
        .then(Transform2D::rotate(rot))
        .then(Transform2D::translate(px / 100.0 * compWidth, py / 100.0 * compHeight));
}

Transform2D resolvedTransform(const Composition& comp, const Layer& layer, double seconds,
                              const TimeContext& ctx, double compWidth, double compHeight,
                              const SizeOf& sizeOf) {
    const auto sizeFor = [&sizeOf](const Layer& l) {
        return sizeOf ? sizeOf(l) : LayerSize{};
    };
    Transform2D out =
        layerTransform(layer, seconds, ctx, compWidth, compHeight, sizeFor(layer));

    // A child is positioned in its parent's space. It inherits the parent's transform,
    // never the parent's dimensions.
    LayerId seen[kMaxParentDepth];
    int depth = 0;
    const Layer* current = &layer;

    while (current->parent.has_value() && depth < kMaxParentDepth) {
        const LayerId parentId = *current->parent;

        // A cycle would otherwise spin here forever, inside the render path, with no
        // error and a frozen window. Bounded and silent: a broken link is a document
        // problem, and this is the wrong place to report it.
        bool repeated = false;
        for (int i = 0; i < depth; ++i) {
            if (seen[i] == parentId) {
                repeated = true;
                break;
            }
        }
        if (repeated || parentId == layer.id) {
            break;
        }

        const Layer* parent = comp.find(parentId);
        if (parent == nullptr) {
            break;  // dangling id; the layer renders unparented
        }
        seen[depth++] = parentId;

        // The parent's own size, not the child's. A parent with an off-centre anchor
        // pivots around a point on ITSELF.
        out = out.then(layerTransform(*parent, seconds, ctx, compWidth, compHeight,
                                      sizeFor(*parent)));
        current = parent;
    }
    return out;
}

bool hasParentCycle(const Composition& comp, LayerId layer) noexcept {
    const Layer* current = comp.find(layer);
    if (current == nullptr) {
        return false;
    }
    LayerId seen[kMaxParentDepth];
    int depth = 0;

    while (current != nullptr && current->parent.has_value()) {
        const LayerId parentId = *current->parent;
        if (parentId == layer) {
            return true;
        }
        for (int i = 0; i < depth; ++i) {
            if (seen[i] == parentId) {
                return true;
            }
        }
        if (depth >= kMaxParentDepth) {
            return true;  // deeper than we will ever walk, so treat it as broken
        }
        seen[depth++] = parentId;
        current = comp.find(parentId);
    }
    return false;
}

bool canParentTo(const Composition& comp, LayerId layer, LayerId candidate) noexcept {
    if (layer == candidate) {
        return false;  // a layer cannot be its own parent
    }
    // Walk up from the candidate. If we reach the layer, then parenting layer to candidate
    // would close a loop.
    const Layer* current = comp.find(candidate);
    int depth = 0;
    while (current != nullptr && current->parent.has_value() && depth < kMaxParentDepth) {
        if (*current->parent == layer) {
            return false;
        }
        current = comp.find(*current->parent);
        ++depth;
    }
    return true;
}

}  // namespace ruby::core
