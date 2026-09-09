#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

// --- GPU abstraction ---------------------------------------------------------
//
// Backend is Dawn (native WebGPU), but nothing above this header knows that.
// A 2D compositor's GPU surface area is small: textures, render passes, compute
// dispatch, blits, buffer uploads. Roughly the operations below. Keeping the
// backend behind this interface means swapping it costs an afternoon rather than
// a rewrite, which matters because Dawn tracks an evolving standard.
//
// STATUS: Dawn backend implemented for device, textures, buffers and command
// submission. Compute pipelines wait on the Slang shader toolchain.

namespace ruby::gpu {

enum class TextureFormat {
    RGBA8Unorm,
    RGBA16Float,   // working format for linear-light compositing
    RGBA32Float,
    R8Unorm,       // masks
    R16Float,
    // Display formats. The sRGB variants make the hardware apply the linear-to-display
    // transform on write, which is why we composite in linear and present to one of
    // these rather than encoding by hand in every shader.
    BGRA8Unorm,
    BGRA8UnormSrgb,
    RGBA8UnormSrgb,
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

class CommandRecorder;

// Opaque resource handles. Backends subclass these; nothing above this header knows
// what a WGPUTexture is.
class Texture {
public:
    virtual ~Texture() = default;
    [[nodiscard]] virtual std::uint32_t width() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t height() const noexcept = 0;
    [[nodiscard]] virtual TextureFormat format() const noexcept = 0;
};

class Buffer {
public:
    virtual ~Buffer() = default;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
};

class ComputePipeline {
public:
    virtual ~ComputePipeline() = default;
};

// A draw pipeline. The engine's own passes (composite, blit) are a handful of these;
// the effect library is compute.
// How a draw combines with what is already in the target.
//
// These are the blend modes expressible as fixed-function GPU state, which is why they
// are a closed enum here rather than something richer. Overlay, Soft Light, Hard Light
// and Difference cannot be written as a blend equation at all: they need the shader to
// read the destination, which is a different mechanism and a separate piece of work.
//
// Everything assumes PREMULTIPLIED source colour. That is what makes the factors below
// compose correctly; with straight alpha, Screen and Add both blow out wherever a layer
// is partly transparent.
enum class BlendPreset {
    AlphaOver,  // Normal
    Add,
    Screen,
    Multiply,
    Lighten,
    Darken,
};

class RenderPipeline {
public:
    virtual ~RenderPipeline() = default;
};

using TextureHandle = std::shared_ptr<Texture>;
using BufferHandle = std::shared_ptr<Buffer>;
using ComputePipelineHandle = std::shared_ptr<ComputePipeline>;
using RenderPipelineHandle = std::shared_ptr<RenderPipeline>;

// A window we can present to. The native handle is the only platform-specific thing
// in the whole interface: an NSView* on macOS, an HWND on Windows.
class Surface {
public:
    virtual ~Surface() = default;

    virtual void configure(std::uint32_t width, std::uint32_t height) = 0;

    // What the swapchain actually gave us. Pipelines have to be built against it.
    [[nodiscard]] virtual TextureFormat format() const noexcept = 0;

    // The next backbuffer, or null if it could not be acquired (resizing, occluded,
    // device lost). Callers must handle null rather than assume a frame is always ready.
    [[nodiscard]] virtual TextureHandle acquire() = 0;

    virtual void present() = 0;
};

using SurfaceHandle = std::shared_ptr<Surface>;

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

    // Effects are authored in Slang and compiled to the backend's target.
    [[nodiscard]] virtual ComputePipelineHandle create_compute_pipeline(
        std::string_view slang_module, std::string_view entry_point) = 0;

    // Engine-internal draw passes, written in WGSL directly. These are a fixed handful
    // that we write once; Slang's module system earns its keep on the effect library,
    // not here.
    [[nodiscard]] virtual RenderPipelineHandle create_render_pipeline(
        std::string_view wgsl, std::string_view vertex_entry,
        std::string_view fragment_entry, TextureFormat target_format,
        std::string_view label, BlendPreset blend = BlendPreset::AlphaOver) = 0;

    // Presentation. `native_window` is an NSView* on macOS, an HWND on Windows.
    [[nodiscard]] virtual SurfaceHandle create_surface(void* native_window) = 0;

    // Work submission
    [[nodiscard]] virtual std::unique_ptr<CommandRecorder> begin_commands(
        std::string_view label) = 0;
    virtual void submit(std::unique_ptr<CommandRecorder> recorder) = 0;

    // Blocks until all submitted work completes. Export path only; never the UI thread.
    virtual void wait_idle() = 0;

    // Human-readable adapter description, for diagnostics and the about box.
    [[nodiscard]] virtual std::string description() const = 0;

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

    // Opens a render pass, clearing the target. Colour components are 0..1 in linear
    // light, which is the working space everything composites in.
    virtual void begin_pass(const TextureHandle& target, float r, float g, float b,
                            float a) = 0;
    virtual void end_pass() = 0;

    // Restricts drawing to a rectangle of the target, in pixels. Everything outside is
    // left untouched, including by the clear, which applies to the whole attachment.
    virtual void set_scissor(std::uint32_t x, std::uint32_t y, std::uint32_t width,
                             std::uint32_t height) = 0;

    // Draws `vertex_count` vertices with the given pipeline and uniform block. Vertices
    // are generated in the shader, so there is no vertex buffer to bind.
    // `texture` is sampled by the pipeline. Callers that want a flat colour bind a 1x1
    // white texture rather than there being two pipelines to keep in step.
    virtual void draw(const RenderPipelineHandle& pipeline, const BufferHandle& uniforms,
                      const TextureHandle& texture, std::uint32_t vertex_count) = 0;
};

// Backend factory. Returns nullptr if no suitable adapter exists.
[[nodiscard]] std::unique_ptr<GpuDevice> create_dawn_device();

}  // namespace ruby::gpu
