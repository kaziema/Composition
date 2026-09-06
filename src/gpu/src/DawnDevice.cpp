#include <dawn/webgpu_cpp.h>

#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>

#include "comp/gpu/GpuDevice.h"

// Dawn backend. This is the only file in the project that knows WebGPU exists;
// everything above talks to GpuDevice. If Dawn ever becomes a liability, a second
// backend lands beside this file and nothing else changes.

namespace comp::gpu {
namespace {

wgpu::TextureFormat toWgpu(TextureFormat format) noexcept {
    switch (format) {
        case TextureFormat::RGBA8Unorm:  return wgpu::TextureFormat::RGBA8Unorm;
        case TextureFormat::RGBA16Float: return wgpu::TextureFormat::RGBA16Float;
        case TextureFormat::RGBA32Float: return wgpu::TextureFormat::RGBA32Float;
        case TextureFormat::R8Unorm:     return wgpu::TextureFormat::R8Unorm;
        case TextureFormat::R16Float:    return wgpu::TextureFormat::R16Float;
    }
    return wgpu::TextureFormat::RGBA16Float;
}

wgpu::TextureUsage toWgpu(TextureUsage usage) noexcept {
    wgpu::TextureUsage out = wgpu::TextureUsage::None;
    if (has(usage, TextureUsage::Sampled))  out |= wgpu::TextureUsage::TextureBinding;
    if (has(usage, TextureUsage::Storage))  out |= wgpu::TextureUsage::StorageBinding;
    if (has(usage, TextureUsage::RenderTo)) out |= wgpu::TextureUsage::RenderAttachment;
    if (has(usage, TextureUsage::CopySrc))  out |= wgpu::TextureUsage::CopySrc;
    if (has(usage, TextureUsage::CopyDst))  out |= wgpu::TextureUsage::CopyDst;
    return out;
}

std::string toString(wgpu::StringView view) {
    return (view.data == nullptr) ? std::string{} : std::string(view.data, view.length);
}

const char* backendName(wgpu::BackendType backend) noexcept {
    switch (backend) {
        case wgpu::BackendType::Metal:    return "Metal";
        case wgpu::BackendType::D3D12:    return "D3D12";
        case wgpu::BackendType::D3D11:    return "D3D11";
        case wgpu::BackendType::Vulkan:   return "Vulkan";
        case wgpu::BackendType::OpenGL:   return "OpenGL";
        case wgpu::BackendType::OpenGLES: return "OpenGLES";
        case wgpu::BackendType::WebGPU:   return "WebGPU";
        case wgpu::BackendType::Null:     return "Null";
        case wgpu::BackendType::Undefined: break;
    }
    return "Unknown";
}

// --- Resources ---------------------------------------------------------------

class DawnTexture final : public Texture {
public:
    DawnTexture(wgpu::Texture texture, const TextureDesc& desc)
        : texture_(std::move(texture)), desc_(desc) {}

    [[nodiscard]] std::uint32_t width() const noexcept override { return desc_.width; }
    [[nodiscard]] std::uint32_t height() const noexcept override { return desc_.height; }
    [[nodiscard]] TextureFormat format() const noexcept override { return desc_.format; }

    [[nodiscard]] const wgpu::Texture& handle() const noexcept { return texture_; }

private:
    wgpu::Texture texture_;
    TextureDesc desc_;
};

class DawnBuffer final : public Buffer {
public:
    DawnBuffer(wgpu::Buffer buffer, std::size_t bytes)
        : buffer_(std::move(buffer)), size_(bytes) {}

    [[nodiscard]] std::size_t size() const noexcept override { return size_; }
    [[nodiscard]] const wgpu::Buffer& handle() const noexcept { return buffer_; }

private:
    wgpu::Buffer buffer_;
    std::size_t size_ = 0;
};

// --- Command recorder --------------------------------------------------------

class DawnRecorder final : public CommandRecorder {
public:
    DawnRecorder(wgpu::Device device, std::string_view label) : device_(std::move(device)) {
        wgpu::CommandEncoderDescriptor desc{};
        desc.label = wgpu::StringView(label.data(), label.size());
        encoder_ = device_.CreateCommandEncoder(&desc);
    }

    void dispatch(const ComputePipelineHandle&, std::uint32_t, std::uint32_t) override {
        // Compute needs a compiled pipeline, which needs Slang. See create_compute_pipeline.
    }

    void bind_texture(std::uint32_t, const TextureHandle&) override {}
    void bind_storage_texture(std::uint32_t, const TextureHandle&) override {}
    void bind_uniforms(std::uint32_t, const BufferHandle&) override {}

    void copy_texture(const TextureHandle& src, const TextureHandle& dst) override {
        const auto* from = dynamic_cast<const DawnTexture*>(src.get());
        const auto* to = dynamic_cast<const DawnTexture*>(dst.get());
        if (from == nullptr || to == nullptr) {
            return;
        }
        wgpu::TexelCopyTextureInfo source{};
        source.texture = from->handle();
        wgpu::TexelCopyTextureInfo destination{};
        destination.texture = to->handle();
        wgpu::Extent3D extent{from->width(), from->height(), 1};
        encoder_.CopyTextureToTexture(&source, &destination, &extent);
    }

    [[nodiscard]] wgpu::CommandBuffer finish() { return encoder_.Finish(); }

private:
    wgpu::Device device_;
    wgpu::CommandEncoder encoder_;
};

// --- Device ------------------------------------------------------------------

class DawnDevice final : public GpuDevice {
public:
    DawnDevice(wgpu::Instance instance, wgpu::Device device, std::string description)
        : instance_(std::move(instance)),
          device_(std::move(device)),
          queue_(device_.GetQueue()),
          description_(std::move(description)) {}

    TextureHandle create_texture(const TextureDesc& desc) override {
        wgpu::TextureDescriptor td{};
        td.label = wgpu::StringView(desc.debug_label.data(), desc.debug_label.size());
        td.dimension = wgpu::TextureDimension::e2D;
        td.size = {desc.width, desc.height, 1};
        td.format = toWgpu(desc.format);
        td.usage = toWgpu(desc.usage);
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        return std::make_shared<DawnTexture>(device_.CreateTexture(&td), desc);
    }

    BufferHandle create_uniform_buffer(std::size_t bytes, std::string_view label) override {
        wgpu::BufferDescriptor bd{};
        bd.label = wgpu::StringView(label.data(), label.size());
        bd.size = bytes;
        bd.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        return std::make_shared<DawnBuffer>(device_.CreateBuffer(&bd), bytes);
    }

    void write_buffer(const BufferHandle& dst, const void* data, std::size_t bytes) override {
        const auto* buffer = dynamic_cast<const DawnBuffer*>(dst.get());
        if (buffer == nullptr) {
            return;
        }
        queue_.WriteBuffer(buffer->handle(), 0, data, bytes);
    }

    void write_texture(const TextureHandle& dst, const void* data, std::size_t bytes,
                       std::uint32_t row_stride) override {
        const auto* texture = dynamic_cast<const DawnTexture*>(dst.get());
        if (texture == nullptr) {
            return;
        }
        wgpu::TexelCopyTextureInfo destination{};
        destination.texture = texture->handle();

        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = row_stride;
        layout.rowsPerImage = texture->height();

        wgpu::Extent3D extent{texture->width(), texture->height(), 1};
        queue_.WriteTexture(&destination, data, bytes, &layout, &extent);
    }

    ComputePipelineHandle create_compute_pipeline(std::string_view, std::string_view) override {
        // Deliberately unimplemented. Effects are authored in Slang (D5) and the shader
        // toolchain is not wired yet. Returning null rather than a stub pipeline so a
        // caller cannot mistake a no-op for a working effect.
        return nullptr;
    }

    std::unique_ptr<CommandRecorder> begin_commands(std::string_view label) override {
        return std::make_unique<DawnRecorder>(device_, label);
    }

    void submit(std::unique_ptr<CommandRecorder> recorder) override {
        auto* dawn = dynamic_cast<DawnRecorder*>(recorder.get());
        if (dawn == nullptr) {
            return;
        }
        wgpu::CommandBuffer commands = dawn->finish();
        queue_.Submit(1, &commands);
    }

    void wait_idle() override {
        const wgpu::Future done = queue_.OnSubmittedWorkDone(
            wgpu::CallbackMode::WaitAnyOnly, [](wgpu::QueueWorkDoneStatus) {});
        instance_.WaitAny(done, UINT64_MAX);
    }

    [[nodiscard]] std::string description() const override { return description_; }

private:
    wgpu::Instance instance_;
    wgpu::Device device_;
    wgpu::Queue queue_;
    std::string description_;
};

}  // namespace

std::unique_ptr<GpuDevice> create_dawn_device() {
    // Timed waits are opt-in. Without this, WaitAny with a timeout fails outright and
    // adapter creation silently returns nothing.
    wgpu::InstanceDescriptor instanceDesc{};
    instanceDesc.capabilities.timedWaitAnyEnable = true;
    instanceDesc.capabilities.timedWaitAnyMaxCount = 8;

    wgpu::Instance instance = wgpu::CreateInstance(&instanceDesc);
    if (instance == nullptr) {
        return nullptr;
    }

    wgpu::RequestAdapterOptions adapterOptions{};
    adapterOptions.powerPreference = wgpu::PowerPreference::HighPerformance;

    wgpu::Adapter adapter;
    wgpu::Future adapterRequest = instance.RequestAdapter(
        &adapterOptions, wgpu::CallbackMode::WaitAnyOnly,
        [&adapter](wgpu::RequestAdapterStatus status, wgpu::Adapter found,
                   wgpu::StringView message) {
            if (status == wgpu::RequestAdapterStatus::Success) {
                adapter = std::move(found);
            } else {
                std::fprintf(stderr, "gpu: no adapter (%s)\n",
                             toString(message).c_str());
            }
        });
    instance.WaitAny(adapterRequest, UINT64_MAX);
    if (adapter == nullptr) {
        return nullptr;
    }

    wgpu::AdapterInfo info{};
    adapter.GetInfo(&info);
    const std::string description = toString(info.device) + " (" +
                                    backendName(info.backendType) + ", " +
                                    toString(info.vendor) + ")";

    wgpu::DeviceDescriptor deviceDesc{};
    deviceDesc.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message) {
            std::fprintf(stderr, "gpu error (%d): %s\n", static_cast<int>(type),
                         toString(message).c_str());
        });
    deviceDesc.SetDeviceLostCallback(
        wgpu::CallbackMode::AllowSpontaneous,
        [](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message) {
            // Destroyed fires on every normal shutdown. Only the other reasons mean
            // something actually went wrong.
            if (reason == wgpu::DeviceLostReason::Destroyed) {
                return;
            }
            std::fprintf(stderr, "gpu device lost: %s\n", toString(message).c_str());
        });

    wgpu::Device device;
    wgpu::Future deviceRequest = adapter.RequestDevice(
        &deviceDesc, wgpu::CallbackMode::WaitAnyOnly,
        [&device](wgpu::RequestDeviceStatus status, wgpu::Device found,
                  wgpu::StringView message) {
            if (status == wgpu::RequestDeviceStatus::Success) {
                device = std::move(found);
            } else {
                std::fprintf(stderr, "gpu: no device (%s)\n", toString(message).c_str());
            }
        });
    instance.WaitAny(deviceRequest, UINT64_MAX);
    if (device == nullptr) {
        return nullptr;
    }

    return std::make_unique<DawnDevice>(std::move(instance), std::move(device),
                                        description);
}

}  // namespace comp::gpu
