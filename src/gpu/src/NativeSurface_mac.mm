#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include "NativeSurface.h"

namespace comp::gpu {
namespace {

// Assigning `view.layer` is ignored once AppKit has made a view layer-backed, and Qt's
// views already are. So instead of replacing the backing layer we add a CAMetalLayer as
// a sublayer and hand Dawn that. The name lets us find it again rather than stacking a
// new one on every call.
NSString* const kLayerName = @"comp.metal";

CAMetalLayer* existingMetalLayer(NSView* view) {
    for (CALayer* sublayer in view.layer.sublayers) {
        if ([sublayer.name isEqualToString:kLayerName] &&
            [sublayer isKindOfClass:[CAMetalLayer class]]) {
            return (CAMetalLayer*)sublayer;
        }
    }
    return nil;
}

}  // namespace

void* prepareNativeSurface(void* native_window) {
    if (native_window == nullptr) {
        return nullptr;
    }

    // Not every platform hands back a real NSView. Qt's offscreen plugin produces a
    // handle that is not a view at all, and bridging it blind wedges the process.
    id object = (__bridge id)native_window;
    if (![object isKindOfClass:[NSView class]]) {
        return nullptr;
    }
    NSView* view = (NSView*)object;
    view.wantsLayer = YES;

    const CGFloat scale = view.window != nil ? view.window.backingScaleFactor : 1.0;

    if (CAMetalLayer* existing = existingMetalLayer(view)) {
        existing.contentsScale = scale;
        existing.frame = view.bounds;
        return (__bridge void*)existing;
    }

    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.name = kLayerName;
    layer.contentsScale = scale;
    layer.frame = view.bounds;
    // Track the view as it resizes; a splitter drag would otherwise stretch stale
    // contents until the next configure lands.
    layer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
    layer.needsDisplayOnBoundsChange = YES;
    [view.layer addSublayer:layer];

    return (__bridge void*)layer;
}

}  // namespace comp::gpu
