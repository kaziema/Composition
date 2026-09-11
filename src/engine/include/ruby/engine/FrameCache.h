#pragma once

#include <cstdint>
#include <list>
#include <unordered_map>

#include "ruby/engine/RenderGraph.h"
#include "ruby/gpu/GpuDevice.h"

namespace ruby::engine {

// The RAM tier of the preview cache (D9).
//
// Holds each layer's finished effect output, keyed on that layer's node hash. Not whole
// frames: a transform is hashed into the composite rather than into the layer, so moving
// a layer costs a recomposite of a handful of quads and re-runs nothing. Compositing is
// milliseconds; four effect passes are not.
//
// Content addressed, never invalidated. Nothing sends this a message saying "layer 3
// changed". A changed input simply produces a different key, the new entry is rendered,
// and the old one ages out. Dependency tracking with explicit invalidation is where cache
// bugs live, and they are the worst kind: the picture is wrong and the app is certain it
// is right.
//
// The disk tier is not here. It needs whole composited frames read back off the GPU, and
// GpuDevice has no readback path at all yet, so that is its own piece of work.
class FrameCache {
public:
    // Bytes, not entries. A 1080x1920 RGBA16F texture is about 16MB, so counting entries
    // would let eight of them quietly become a gigabyte.
    explicit FrameCache(std::size_t budgetBytes);

    // Null on a miss. A hit moves the entry to the front of the eviction order.
    [[nodiscard]] gpu::TextureHandle find(NodeHash key);

    // Ignored when a single entry is larger than the whole budget, rather than evicting
    // everything to make room for something that will immediately evict itself.
    void put(NodeHash key, gpu::TextureHandle texture, std::size_t bytes);

    // Everything goes. For a resolution change or a project close, where every key is
    // stale but nothing would tell the cache so.
    void clear();

    void setBudget(std::size_t bytes);

    struct Stats {
        std::size_t entries = 0;
        std::size_t bytes = 0;
        std::size_t budget = 0;
        std::uint64_t hits = 0;
        std::uint64_t misses = 0;
        std::uint64_t evictions = 0;
    };
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }
    void resetCounters() noexcept;

    // Whether a key is held, without counting as a hit or moving it up the order.
    //
    // For the cache bar in the ruler: drawing what is ready must not change what is ready,
    // or the act of looking at the cache reorders it.
    [[nodiscard]] bool contains(NodeHash key) const noexcept;

private:
    struct Entry {
        NodeHash key = 0;
        gpu::TextureHandle texture;
        std::size_t bytes = 0;
    };

    void evictTo(std::size_t bytes);

    std::list<Entry> order_;  // most recently used at the front
    std::unordered_map<NodeHash, std::list<Entry>::iterator> index_;
    Stats stats_;
};

}  // namespace ruby::engine
