#include "comp/engine/EffectRegistry.h"

#include <algorithm>

namespace comp::engine {
namespace {

using core::ParamType;
using core::SpatialUnit;

// Shared prologue for every effect shader. A fullscreen triangle rather than a quad:
// three vertices instead of six, no seam down the diagonal, and it is the standard way
// to run a fragment pass over a whole texture.
constexpr const char* kEffectPrologue = R"(
struct EffectUniforms {
    params : array<vec4<f32>, 8>,
};
@group(0) @binding(0) var<uniform> u    : EffectUniforms;
@group(0) @binding(1) var        samp : sampler;
@group(0) @binding(2) var        tex  : texture_2d<f32>;

struct VsOut {
    @builtin(position) position : vec4<f32>,
    @location(0)       uv       : vec2<f32>,
};

@vertex
fn vs(@builtin(vertex_index) index : u32) -> VsOut {
    var corners = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    var out : VsOut;
    out.position = vec4<f32>(corners[index], 0.0, 1.0);
    // Clip space is y-up, texture space is y-down.
    out.uv = vec2<f32>((corners[index].x + 1.0) * 0.5, (1.0 - corners[index].y) * 0.5);
    return out;
}

// Rec.709 luminance. Correct because we work in linear light, not gamma-encoded values.
fn luma(c : vec3<f32>) -> f32 {
    return dot(c, vec3<f32>(0.2126, 0.7152, 0.0722));
}
)";

core::ParamSpec param(std::string key, std::string label, SpatialUnit unit, double value,
                      int order) {
    core::ParamSpec spec;
    spec.key = std::move(key);
    spec.label = std::move(label);
    spec.order = order;
    spec.type = ParamType::Float;
    spec.unit = unit;
    spec.default_value = value;
    spec.introduced_in_schema = 1;
    return spec;
}

EffectDef makeGrade() {
    EffectDef def;
    def.schema.id = "core.color.grade";
    def.schema.schema = 1;
    def.schema.display_name = "Grade";
    def.schema.params = {
        param("exposure", "Exposure", SpatialUnit::Normalized, 0.0, 0),
        param("contrast", "Contrast", SpatialUnit::Percent, 100.0, 1),
        param("saturation", "Saturation", SpatialUnit::Percent, 100.0, 2),
    };

    // Every operation is in linear light, which is the whole reason this can look like a
    // plugin rather than like AE's stock Brightness & Contrast (F2). Contrast pivots
    // around 18% grey, the scene-referred mid point, instead of around 0.5.
    def.shader = std::string(kEffectPrologue) + R"(
@fragment
fn fs(in : VsOut) -> @location(0) vec4<f32> {
    var c = textureSample(tex, samp, in.uv);

    let exposure   = u.params[0].x;
    let contrast   = u.params[1].x * 0.01;
    let saturation = u.params[2].x * 0.01;

    c = vec4<f32>(c.rgb * pow(2.0, exposure), c.a);
    c = vec4<f32>((c.rgb - vec3<f32>(0.18)) * contrast + vec3<f32>(0.18), c.a);
    c = vec4<f32>(mix(vec3<f32>(luma(c.rgb)), c.rgb, saturation), c.a);

    return vec4<f32>(max(c.rgb, vec3<f32>(0.0)), c.a);
}
)";
    return def;
}

}  // namespace

EffectRegistry::EffectRegistry() { effects_.push_back(makeGrade()); }

const EffectRegistry& EffectRegistry::instance() {
    static const EffectRegistry registry;
    return registry;
}

const EffectDef* EffectRegistry::find(std::string_view id) const noexcept {
    const auto it = std::find_if(effects_.begin(), effects_.end(),
                                 [id](const EffectDef& d) { return d.schema.id == id; });
    return it == effects_.end() ? nullptr : &*it;
}

core::EffectInstance EffectRegistry::instantiate(std::string_view id) const {
    core::EffectInstance instance;
    const EffectDef* def = find(id);
    if (def == nullptr) {
        return instance;
    }

    instance.effectId = def->schema.id;
    instance.schema = def->schema.schema;
    instance.displayName = def->schema.display_name;

    for (const core::ParamSpec& spec : def->schema.params) {
        core::Property property;
        property.key = spec.key;
        property.label = spec.label;
        property.group = def->schema.display_name;
        property.unit = spec.unit;
        property.staticValue = core::Value::scalar(spec.default_value);
        instance.params.push_back(std::move(property));
    }
    return instance;
}

}  // namespace comp::engine
