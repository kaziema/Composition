// Brings the real GPU up. Not a mock: this creates a Dawn device against the actual
// adapter, allocates on it, and submits work. If the graphics stack is broken, this
// is where it should say so.

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "comp/gpu/GpuDevice.h"

using namespace comp::gpu;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

}  // namespace

int main() {
    std::unique_ptr<GpuDevice> device = create_dawn_device();
    check(device != nullptr, "a GPU device comes up");
    if (device == nullptr) {
        std::fprintf(stderr, "\nno adapter available; the rest cannot run\n");
        return EXIT_FAILURE;
    }

    std::printf("adapter: %s\n", device->description().c_str());
    check(!device->description().empty(), "the adapter identifies itself");

    // A working-format texture, the shape everything composites into.
    TextureDesc desc;
    desc.width = 1080;
    desc.height = 1920;
    desc.format = TextureFormat::RGBA16Float;
    desc.usage = TextureUsage::Sampled | TextureUsage::RenderTo | TextureUsage::CopySrc;
    desc.debug_label = "test target";

    TextureHandle target = device->create_texture(desc);
    check(target != nullptr, "a 1080x1920 RGBA16F texture allocates");
    if (target != nullptr) {
        check(target->width() == 1080, "texture reports its width");
        check(target->height() == 1920, "texture reports its height");
        check(target->format() == TextureFormat::RGBA16Float, "texture reports its format");
    }

    // Uniform buffer plus an upload, the path every effect's parameters take.
    BufferHandle uniforms = device->create_uniform_buffer(256, "test uniforms");
    check(uniforms != nullptr, "a uniform buffer allocates");
    if (uniforms != nullptr) {
        check(uniforms->size() == 256, "buffer reports its size");
        const std::vector<float> values(64, 1.0f);
        device->write_buffer(uniforms, values.data(), values.size() * sizeof(float));
    }

    // Copy between two textures, then block until the GPU has actually done it.
    TextureDesc copyDesc = desc;
    copyDesc.usage = TextureUsage::CopyDst | TextureUsage::Sampled;
    copyDesc.debug_label = "test copy";
    TextureHandle copy = device->create_texture(copyDesc);
    check(copy != nullptr, "a second texture allocates");

    auto recorder = device->begin_commands("test submit");
    check(recorder != nullptr, "a command recorder opens");
    if (recorder != nullptr && target != nullptr && copy != nullptr) {
        recorder->copy_texture(target, copy);
        device->submit(std::move(recorder));
        device->wait_idle();
    }

    // Documented gap, asserted so it cannot be silently "fixed" into a no-op stub.
    check(device->create_compute_pipeline("noop", "main") == nullptr,
          "compute pipelines are not implemented yet and say so by returning null");

    if (failures != 0) {
        std::fprintf(stderr, "\n%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("gpu: all checks passed");
    return EXIT_SUCCESS;
}
