#include "ruby/engine/EffectRegistry.h"

#include <algorithm>
#include <string>
#include <cctype>

namespace ruby::engine {
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

// Every parameter states its bounds. The range argument is not defaulted on purpose: an
// unbounded parameter should be something someone decided, written as
// ParamRange::unbounded, rather than something nobody filled in.
//
// These are all schema 1. Ruby has never shipped, so there is no earlier version of any
// of these schemas anywhere on disk or in anyone's project, and no v1-without-ranges for
// a tightened limit to break. Schema 1 with ranges is the baseline the schema files in
// task 2 will record. Every tightening after that costs a bump, which validate_against_
// previous now enforces.
core::ParamSpec param(std::string key, std::string label, SpatialUnit unit, double value,
                      int order, core::ParamRange range) {
    core::ParamSpec spec;
    spec.key = std::move(key);
    spec.label = std::move(label);
    spec.order = order;
    spec.type = ParamType::Float;
    spec.unit = unit;
    spec.default_value = value;
    spec.range = range;
    spec.introduced_in_schema = 1;
    return spec;
}

using R = core::ParamRange;

EffectDef makeGrade() {
    EffectDef def;
    def.schema.id = "core.color.grade";
    def.schema.schema = 1;
    def.schema.display_name = "Grade";
    def.schema.params = {
        param("exposure", "Exposure", SpatialUnit::Normalized, 0.0, 0,
              // Stops. Past six either way the picture is white or black, but there is
              // no reason to forbid it: a grade can be a deliberate blowout.
              R::unbounded(-6.0, 6.0)),
        param("contrast", "Contrast", SpatialUnit::Percent, 100.0, 1,
              // Zero is flat grey and negative inverts, which is a real look.
              R::unbounded(0.0, 300.0)),
        param("saturation", "Saturation", SpatialUnit::Percent, 100.0, 2,
              // Floored at zero: negative saturation is channel inversion wearing a
              // saturation label, and there is an invert effect for that.
              R::atLeast(0.0, 300.0)),
    };

    // Every operation is in linear light, which is the whole reason this can look like a
    // plugin rather than like AE's stock Brightness & Contrast. Contrast pivots
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


// --- The library -------------------------------------------------------------
//
// Chosen from the teardown in NOTEBOOK 7: these are what the edit community actually
// reaches for. Not a general compositing toolkit, a set of things that make a cut hit.

// Gaussian in two passes would be faster and needs a ping-pong the effect stack does not
// expose yet. A single pass with a fixed tap count is honest about being a first version,
// and at these radii the difference is not visible.
EffectDef makeBlur() {
    EffectDef def;
    def.schema.id = "core.blur.gaussian";
    def.schema.schema = 1;
    def.schema.display_name = "Gaussian Blur";
    def.schema.params = {
        param("radius", "Radius", SpatialUnit::PercentOfWidth, 0.0, 0,
              // A negative blur radius is not a thing. Nine taps per axis, so past about
              // 10% of frame width it is banding rather than blur.
              R::atLeast(0.0, 10.0)),
    };
    def.shader = std::string(kEffectPrologue) + R"(
@fragment
fn fs(in : VsOut) -> @location(0) vec4<f32> {
    let radius = u.params[0].x * 0.01;
    if (radius <= 0.0) {
        return textureSample(tex, samp, in.uv);
    }
    // Nine taps on each axis in one pass. Weights are a normalised Gaussian; the sum is
    // divided out rather than hardcoded so changing the taps cannot change the exposure.
    var total = vec4<f32>(0.0);
    var weightSum = 0.0;
    for (var y = -4; y <= 4; y = y + 1) {
        for (var x = -4; x <= 4; x = x + 1) {
            let offset = vec2<f32>(f32(x), f32(y)) * radius * 0.25;
            let d = f32(x * x + y * y);
            let w = exp(-d / 8.0);
            total = total + textureSample(tex, samp, in.uv + offset) * w;
            weightSum = weightSum + w;
        }
    }
    return total / weightSum;
}
)";
    return def;
}

// The one everybody wants. Splits the channels apart along an angle, which is the whole
// look of a shake edit's impact frame.
EffectDef makeChromatic() {
    EffectDef def;
    def.schema.id = "core.distort.chromatic";
    def.schema.schema = 1;
    def.schema.display_name = "Chromatic Aberration";
    def.schema.params = {
        param("amount", "Amount", SpatialUnit::PercentOfWidth, 0.0, 0,
              // Channel separation. Negative just swaps which way red and blue go.
              R::unbounded(-5.0, 5.0)),
        param("angle", "Angle", SpatialUnit::Degrees, 0.0, 1,
              // Wraps, so a limit would be a lie. The slider covers one turn.
              R::unbounded(0.0, 360.0)),
    };
    def.shader = std::string(kEffectPrologue) + R"(
@fragment
fn fs(in : VsOut) -> @location(0) vec4<f32> {
    let amount = u.params[0].x * 0.01;
    let angle = radians(u.params[1].x);
    let dir = vec2<f32>(cos(angle), sin(angle)) * amount;

    // Red and blue pull apart in opposite directions and green stays put, so the image
    // does not appear to shift as a whole. Alpha comes from the centre for the same
    // reason: a split alpha fringes the edge of every layer.
    let r = textureSample(tex, samp, in.uv + dir).r;
    let g = textureSample(tex, samp, in.uv);
    let b = textureSample(tex, samp, in.uv - dir).b;
    return vec4<f32>(r, g.g, b, g.a);
}
)";
    return def;
}

// Bloom on the bright parts only. Threshold in linear light, which is why it picks
// highlights rather than everything mid-grey and up.
EffectDef makeGlow() {
    EffectDef def;
    def.schema.id = "core.glow.bloom";
    def.schema.schema = 1;
    def.schema.display_name = "Glow";
    def.schema.params = {
        param("threshold", "Threshold", SpatialUnit::Normalized, 1.0, 0,
              // Linear light, so above 1.0 is the highlight range this is meant to pick
              // out. Hard floor at zero: below it everything glows and it is not a glow.
              R::atLeast(0.0, 4.0)),
        param("radius", "Radius", SpatialUnit::PercentOfWidth, 2.0, 1,
              R::atLeast(0.0, 20.0)),
        param("intensity", "Intensity", SpatialUnit::Percent, 100.0, 2,
              R::atLeast(0.0, 400.0)),
    };
    def.shader = std::string(kEffectPrologue) + R"(
@fragment
fn fs(in : VsOut) -> @location(0) vec4<f32> {
    let threshold = u.params[0].x;
    let radius    = u.params[1].x * 0.01;
    let intensity = u.params[2].x * 0.01;

    let base = textureSample(tex, samp, in.uv);
    var bloom = vec3<f32>(0.0);
    var weightSum = 0.0;
    for (var y = -3; y <= 3; y = y + 1) {
        for (var x = -3; x <= 3; x = x + 1) {
            let offset = vec2<f32>(f32(x), f32(y)) * radius * 0.33;
            let s = textureSample(tex, samp, in.uv + offset).rgb;
            // Only what is over the threshold contributes, and by how far over it is.
            let over = max(s - vec3<f32>(threshold), vec3<f32>(0.0));
            let w = exp(-f32(x * x + y * y) / 6.0);
            bloom = bloom + over * w;
            weightSum = weightSum + w;
        }
    }
    return vec4<f32>(base.rgb + bloom / weightSum * intensity, base.a);
}
)";
    return def;
}

// Directional smear. What a whip pan or a punch-in wants, and cheap because it is one
// axis rather than a kernel.
EffectDef makeMotionBlur() {
    EffectDef def;
    def.schema.id = "core.blur.directional";
    def.schema.schema = 1;
    def.schema.display_name = "Directional Blur";
    def.schema.params = {
        param("length", "Length", SpatialUnit::PercentOfWidth, 0.0, 0,
              R::atLeast(0.0, 15.0)),
        param("angle", "Angle", SpatialUnit::Degrees, 0.0, 1,
              // Wraps, so a limit would be a lie. The slider covers one turn.
              R::unbounded(0.0, 360.0)),
    };
    def.shader = std::string(kEffectPrologue) + R"(
@fragment
fn fs(in : VsOut) -> @location(0) vec4<f32> {
    let len = u.params[0].x * 0.01;
    let angle = radians(u.params[1].x);
    if (len <= 0.0) {
        return textureSample(tex, samp, in.uv);
    }
    let step = vec2<f32>(cos(angle), sin(angle)) * len / 12.0;
    var total = vec4<f32>(0.0);
    // Centred on the pixel rather than trailing from it, so the layer does not appear to
    // move when the effect is applied.
    for (var i = -6; i <= 6; i = i + 1) {
        total = total + textureSample(tex, samp, in.uv + step * f32(i));
    }
    return total / 13.0;
}
)";
    return def;
}

// Analogue grade in one control. Lift, gamma and gain is what a colourist reaches for and
// what every LUT is approximating.
EffectDef makeLiftGammaGain() {
    EffectDef def;
    def.schema.id = "core.color.liftgammagain";
    def.schema.schema = 1;
    def.schema.display_name = "Lift Gamma Gain";
    def.schema.params = {
        param("lift", "Lift", SpatialUnit::Normalized, 0.0, 0,
              // Added to the blacks. Small numbers, and the slider says so.
              R::unbounded(-0.5, 0.5)),
        param("gamma", "Gamma", SpatialUnit::Normalized, 1.0, 1,
              // An exponent. Zero or below is a divide by zero or a sign flip in pow,
              // so the floor here is protecting the shader, not taste.
              R::atLeast(0.01, 4.0)),
        param("gain", "Gain", SpatialUnit::Normalized, 1.0, 2,
              R::atLeast(0.0, 4.0)),
    };
    def.shader = std::string(kEffectPrologue) + R"(
@fragment
fn fs(in : VsOut) -> @location(0) vec4<f32> {
    let lift  = u.params[0].x;
    let gamma = max(u.params[1].x, 0.01);
    let gain  = u.params[2].x;

    let c = textureSample(tex, samp, in.uv);
    var v = max(c.rgb + vec3<f32>(lift), vec3<f32>(0.0));
    v = pow(v, vec3<f32>(1.0 / gamma));
    v = v * gain;
    return vec4<f32>(max(v, vec3<f32>(0.0)), c.a);
}
)";
    return def;
}

// Vignette and grain in one, because they are always used together and both are about
// making a clean digital image look like it came from somewhere.
EffectDef makeVignette() {
    EffectDef def;
    def.schema.id = "core.stylize.vignette";
    def.schema.schema = 1;
    def.schema.display_name = "Vignette";
    def.schema.params = {
        param("amount", "Amount", SpatialUnit::Percent, 0.0, 0,
              // How dark the corners go. Fully bounded: past 100 there is nothing left
              // to darken and the control stops meaning anything.
              R::between(0.0, 100.0)),
        param("softness", "Softness", SpatialUnit::Percent, 50.0, 1,
              R::between(0.0, 100.0)),
    };
    def.shader = std::string(kEffectPrologue) + R"(
@fragment
fn fs(in : VsOut) -> @location(0) vec4<f32> {
    let amount = u.params[0].x * 0.01;
    let softness = max(u.params[1].x * 0.01, 0.01);

    let c = textureSample(tex, samp, in.uv);
    // Distance from centre, normalised so the corners are 1.
    let d = length(in.uv - vec2<f32>(0.5)) / 0.7071;
    let falloff = 1.0 - smoothstep(1.0 - softness, 1.0, d) * amount;
    return vec4<f32>(c.rgb * falloff, c.a);
}
)";
    return def;
}

// Posterised colour and a hard luminance edge. The "anime" look, and the cheapest way to
// make footage stop looking like footage.
EffectDef makePosterize() {
    EffectDef def;
    def.schema.id = "core.stylize.posterize";
    def.schema.schema = 1;
    def.schema.display_name = "Posterize";
    def.schema.params = {
        param("levels", "Levels", SpatialUnit::Normalized, 8.0, 0,
              // Quantisation steps. One is a solid colour, and below one the maths is
              // meaningless, so this one is a genuine hard floor.
              R::atLeast(1.0, 32.0)),
    };
    def.shader = std::string(kEffectPrologue) + R"(
@fragment
fn fs(in : VsOut) -> @location(0) vec4<f32> {
    let levels = max(u.params[0].x, 2.0);
    let c = textureSample(tex, samp, in.uv);
    return vec4<f32>(floor(c.rgb * levels) / levels, c.a);
}
)";
    return def;
}

}  // namespace

std::string effectCategory(std::string_view id) {
    const std::size_t first = id.find('.');
    if (first == std::string_view::npos) {
        return "Other";
    }
    const std::size_t second = id.find('.', first + 1);
    if (second == std::string_view::npos) {
        return "Other";
    }
    std::string name(id.substr(first + 1, second - first - 1));
    if (!name.empty()) {
        name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    }
    return name;
}

EffectRegistry::EffectRegistry() {
    // Order here is the order they appear under each category in the menu.
    effects_.push_back(makeGrade());
    effects_.push_back(makeLiftGammaGain());
    effects_.push_back(makeBlur());
    effects_.push_back(makeMotionBlur());
    effects_.push_back(makeChromatic());
    effects_.push_back(makeGlow());
    effects_.push_back(makeVignette());
    effects_.push_back(makePosterize());
}

const EffectRegistry& EffectRegistry::instance() {
    static const EffectRegistry registry;
    return registry;
}

const EffectDef* EffectRegistry::find(std::string_view id) const noexcept {
    const auto it = std::find_if(effects_.begin(), effects_.end(),
                                 [id](const EffectDef& d) { return d.schema.id == id; });
    return it == effects_.end() ? nullptr : &*it;
}

EffectRegistryMigrationReport EffectRegistry::migrate(
    core::EffectInstance& instance) const {
    const EffectDef* def = find(instance.effectId);
    if (def == nullptr) {
        // The loader already said the effect is unknown. Nothing to migrate onto, and the
        // parameters are left exactly as they were so a later version of the app that does
        // know this effect can still read them.
        EffectRegistryMigrationReport report;
        report.ok = false;
        report.from_schema = instance.schema;
        report.to_schema = instance.schema;
        return report;
    }
    return migrateAgainst(*def, instance);
}

EffectRegistryMigrationReport migrateAgainst(const EffectDef& definition,
                                             core::EffectInstance& instance) {
    const EffectDef* def = &definition;
    EffectRegistryMigrationReport report;
    report.from_schema = instance.schema;
    report.to_schema = instance.schema;

    const int current = def->schema.schema;
    const int authored = instance.schema > 0 ? instance.schema : 1;

    if (authored > current) {
        // Content from a newer app. Refused rather than guessed at: the parameters may
        // mean something this build does not know, and interpreting them with today's
        // schema is how you silently change someone's work.
        report.ok = false;
        report.notes.push_back(instance.effectId + " was authored at schema " +
                               std::to_string(authored) + " and this build only knows " +
                               std::to_string(current) +
                               "; its parameters were left untouched");
        return report;
    }

    // 1. The bag: what the file actually had, whatever the schema thinks of it now.
    core::ParamBag bag;
    for (const core::Property& p : instance.params) {
        core::ParamBag::Entry e;
        e.value = p.staticValue;
        e.keys = p.keys;
        e.expression = p.expression;
        bag.set(p.key, std::move(e));
    }

    if (authored < current) {
        core::runMigrations(def->migrations, authored, current, bag);
        report.notes.push_back(instance.effectId + " migrated from schema " +
                               std::to_string(authored) + " to " +
                               std::to_string(current));
    }

    // 2 and 3 together: rebuild the parameter list in schema order, taking each key from
    // the bag when the content had it. Anything left in the bag afterwards is a key no
    // current parameter claims, which is either retired or from a version that renamed it.
    std::vector<core::Property> rebuilt;
    rebuilt.reserve(def->schema.params.size());

    for (const core::ParamSpec& spec : def->schema.params) {
        core::Property p;
        p.key = spec.key;
        p.label = spec.label;
        p.unit = spec.unit;
        p.range = spec.range;
        p.group = spec.group.empty() ? def->schema.display_name : spec.group;

        if (const core::ParamBag::Entry* e = bag.find(spec.key); e != nullptr) {
            p.staticValue = e->value;
            p.keys = e->keys;
            p.expression = e->expression;
            bag.remove(spec.key);
        } else if (spec.introduced_in_schema > authored && spec.legacy_default.has_value()) {
            // THE rule this whole harness exists for. A parameter added in schema 3,
            // loading into content authored at schema 1, must take the value that keeps
            // that content looking the way it did, not today's better default. Improving
            // a default should never silently rewrite saved work.
            //
            // AE calls this PF_ParamFlag_USE_VALUE_FOR_OLD_PROJECTS and learned it the
            // hard way; validate() refuses a post-v1 parameter that has no legacy_default
            // precisely so this branch always has something to use.
            p.staticValue = core::Value::scalar(*spec.legacy_default);
            report.notes.push_back(instance.effectId + "." + spec.key +
                                   " did not exist at schema " + std::to_string(authored) +
                                   "; filled in with its legacy default");
        } else {
            p.staticValue = core::Value::scalar(spec.default_value);
        }
        rebuilt.push_back(std::move(p));
    }

    for (const auto& [key, entry] : bag.entries()) {
        (void)entry;
        // Silently for a retired key, which is D1's rule: it was deliberately removed and
        // the user does not need to hear about a decision made years ago. Noted for a key
        // nobody recognises, because that is either a corrupt file or a bug in a migration
        // and both are worth seeing.
        if (!def->schema.is_retired(key)) {
            report.notes.push_back(instance.effectId + " had an unknown parameter \"" +
                                   key + "\", which was dropped");
        }
    }

    instance.params = std::move(rebuilt);
    instance.schema = current;
    instance.displayName = def->schema.display_name;
    report.to_schema = current;
    return report;
}

void EffectRegistry::adoptSchema(core::EffectInstance& instance) const {
    const EffectDef* def = find(instance.effectId);
    if (def == nullptr) {
        return;  // unknown effect; the loader already reported it
    }
    for (core::Property& p : instance.params) {
        if (const core::ParamSpec* spec = def->schema.find(p.key); spec != nullptr) {
            p.range = spec->range;
            if (!spec->group.empty()) {
                p.group = spec->group;
            }
        }
    }
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
        // An empty group in the schema means "this effect's own name", which is what
        // every parameter wanted before groups existed and still what most want.
        property.group = spec.group.empty() ? def->schema.display_name : spec.group;
        property.unit = spec.unit;
        property.range = spec.range;
        property.staticValue = core::Value::scalar(spec.default_value);
        instance.params.push_back(std::move(property));
    }
    return instance;
}

}  // namespace ruby::engine
