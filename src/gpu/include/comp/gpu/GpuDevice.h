#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

// --- GPU abstraction (decision D5) -------------------------------------------
//
// Backend is Dawn (native WebGPU), but nothing above this header knows that.
// A 2D compositor's GPU surface area is small: textures, render passes, compute
// dispatch, blits, buffer uploads. Roughly the operations below. Keeping the
// backend behind this interface means swapping it costs an afternoon rather than
// a rewrite, which matters because Dawn tracks an evolving standard.
//
// STATUS: interface only. No implementation yet. Dawn gets wired in behind this.

namespace comp::gpu {

enum class TextureFormat {
    RGBA8Unorm,
    RGBA16Float,   // working format for linear-light compositing
    RGBA32Float,
    R8Unorm,       // masks
    R16Float,
};

enum class TextureUsage : std::uint32_t {
    None       = 0,
    Sampled    = 1u << 0,
    Storage    = 1u << 1,
    RenderTo   = 1u << 2,
    CopySrc    = 1u << 3,
    CopyDst    = 1u << 4,
};

constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) noexcept {
    return static_cast<TextureUsage>(static_cast<std::uint32_t>(a) |
                                     static_cast<std::uint32_t>(b));
}
constexpr bool has(TextureUsage set, TextureUsage bit) noexcept {
    return (static_cast<std::uint32_t>(set) & static_cast<std::uint32_t>(bit)) != 0u;
}

struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureFormat format = TextureFormat::RGBA16Float;
    TextureUsage usage = TextureUsage::Sampled | TextureUsage::RenderTo;
    std::string_view debug_label;
};

class Texture;
class Buffer;
class ComputePipeline;
class CommandRecorder;

// Opaque handles owned by the device.
using TextureHandle = std::shared_ptr<Texture>;
using BufferHandle = std::shared_ptr<Buffer>;
using ComputePipelineHandle = std::shared_ptr<ComputePipeline>;

class GpuDevice {
public:
    virtual ~GpuDevice() = default;

    GpuDevice(const GpuDevice&) = delete;
    GpuDevice& operator=(const GpuDevice&) = delete;

    // Resources
    [[nodiscard]] virtual TextureHandle create_texture(const TextureDesc& desc) = 0;
    [[nodiscard]] virtual BufferHandle create_uniform_buffer(std::size_t bytes,
                                                            std::string_view label) = 0;
    virtual void write_buffer(const BufferHandle& dst, const void* data, std::size_t bytes) = 0;
    virtual void write_texture(const TextureHandle& dst, const void* data,
                               std::size_t bytes, std::uint32_t row_stride) = 0;

    // Shaders. Source is Slang, compiled to the backend's target offline or on load.
    [[nodiscard]] virtual ComputePipelineHandle create_compute_pipeline(
        std::string_view slang_module, std::string_view entry_point) = 0;

    // Work submission
    [[nodiscard]] virtual std::unique_ptr<CommandRecorder> begin_commands(
        std::string_view label) = 0;
    virtual void submit(std::unique_ptr<CommandRecorder> recorder) = 0;

    // Blocks until all submitted work completes. Export path only; never the UI thread.
    virtual void wait_idle() = 0;

protected:
    GpuDevice() = default;
};

// Records a batch of GPU work. One recorder per render of one frame region.
class CommandRecorder {
public:
    virtual ~CommandRecorder() = default;

    virtual void dispatch(const ComputePipelineHandle& pipeline,
                          std::uint32_t groups_x,
                          std::uint32_t groups_y) = 0;

    virtual void bind_texture(std::uint32_t slot, const TextureHandle& tex) = 0;
    virtual void bind_storage_texture(std::uint32_t slot, const TextureHandle& tex) = 0;
    virtual void bind_uniforms(std::uint32_t slot, const BufferHandle& buf) = 0;

    virtual void copy_texture(const TextureHandle& src, const TextureHandle& dst) = 0;
};

// Backend factory. Returns nullptr if no suitable adapter exists.
[[nodiscard]] std::unique_ptr<GpuDevice> create_dawn_device();

}  // namespace comp::gpu
