// Saving and loading a project. The round trip has to be lossless for anything a user
// can change, and the loader has to survive files that are wrong in the ways real files
// go wrong: hand-edited, truncated, written by a different version, referencing media
// that has since been removed.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "ruby/io/ProjectIO.h"

using namespace ruby;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

core::Project makeProject() {
    core::Project p;
    core::MediaItem& clip =
        p.addMedia("/clips/a.mp4", "a.mp4", core::MediaKind::Video, 12.5, 1920, 1080,
                   23.976, true);

    core::Composition& comp = p.addComposition("sneaker", 1080, 1920, 29.97, 14.5);
    comp.rhythm.setLane(core::MarkerLane::Vocal,
                        {{2.167, core::MarkerLane::Vocal, 0.84f, 0},
                         {3.083, core::MarkerLane::Vocal, 0.61f, 1}});
    comp.rhythm.addUserMarker(5.5);

    core::Layer& layer = p.addLayer(comp, "a.mp4", core::LayerKind::Footage);
    layer.media = clip.id;
    layer.blend = core::BlendMode::Add;
    layer.inPoint = core::TimeValue::beats(2.0);
    layer.outPoint = core::TimeValue::seconds(9.25);
    layer.expanded = true;
    // The eye and the speaker are separate switches, so a project has to be able to say
    // "visible but muted" and have that mean something after a reload.
    layer.enabled = true;
    layer.audioEnabled = false;

    const core::TimeContext ctx = comp.timeContext();
    if (core::Property* pos = layer.find("position"); pos != nullptr) {
        pos->addKey({core::TimeValue::seconds(0.6), core::Value::vec2(50.0, 88.0),
                     core::Interpolation::Bezier, 0.68, 0.68, 0.12},
                    ctx);
        pos->addKey({core::TimeValue::beats(4.0), core::Value::vec2(50.0, 42.0),
                     core::Interpolation::Hold, 0.0, 0.0, 0.0},
                    ctx);
        pos->expression = "wiggle(30, 10)";
    }

    core::EffectInstance grade;
    grade.effectId = "core.color.grade";
    grade.schema = 1;
    grade.displayName = "Grade";
    core::Property sat;
    sat.key = "saturation";
    sat.label = "Saturation";
    sat.group = "Grade";
    sat.unit = core::SpatialUnit::Percent;
    sat.staticValue = core::Value::scalar(160.0);
    grade.params.push_back(sat);
    layer.effects.push_back(grade);

    return p;
}

}  // namespace

int main() {
    const core::Project original = makeProject();

    core::Project loaded;
    io::LoadReport report = io::fromJson(loaded, io::toJson(original));
    check(report.ok, "a project we just wrote loads back");
    check(report.clean(), "and does so without complaints");

    check(loaded.media().size() == 1, "media survives");
    check(loaded.media()[0].path == "/clips/a.mp4", "the path survives");
    check(std::fabs(loaded.media()[0].fps - 23.976) < 1e-9,
          "a non-integer frame rate survives exactly");

    check(loaded.compositions().size() == 1, "the composition survives");
    const core::Composition& comp = loaded.compositions().front();
    check(comp.width == 1080 && comp.height == 1920, "size survives");
    check(std::fabs(comp.fps - 29.97) < 1e-9, "29.97 survives exactly");

    check(comp.layers.size() == 1, "the layer survives");
    const core::Layer& layer = comp.layers.front();
    check(layer.blend == core::BlendMode::Add, "blend mode survives");
    check(layer.expanded, "twirl state survives");
    check(layer.enabled, "the eye survives");
    check(!layer.audioEnabled, "and the speaker survives independently of it");

    // A project written before the speaker switch existed has no audioEnabled key. It
    // must open audible: defaulting to false would silently mute every old project.
    {
        std::string json = io::toJson(original);
        const std::string key = "\"audioEnabled\": false,";
        const auto at = json.find(key);
        check(at != std::string::npos, "the field is actually written");
        json.erase(at, key.size());

        core::Project old_;
        const io::LoadReport r = io::fromJson(old_, json);
        check(r.ok, "a project without the field still loads");
        check(old_.compositions().front().layers.front().audioEnabled,
              "and its layers default to audible");
    }

    // A time authored in beats must not come back as seconds.
    check(layer.inPoint.mode == core::TimeMode::Beats, "beats stay beats");
    check(std::fabs(layer.inPoint.value - 2.0) < 1e-9, "and keep their value");
    check(layer.outPoint.mode == core::TimeMode::Seconds, "seconds stay seconds");

    check(layer.media.has_value(), "the media link survives");
    check(layer.media.has_value() &&
              ruby::core::Project(loaded).findMedia(*layer.media) != nullptr,
          "and still resolves");

    const core::Property* pos = layer.find("position");
    check(pos != nullptr, "the property survives");
    check(pos != nullptr && pos->keys.size() == 2, "both keyframes survive");
    if (pos != nullptr && pos->keys.size() == 2) {
        check(pos->keys[0].value.count == 2, "a vec2 keyframe stays a vec2");
        check(std::fabs(pos->keys[0].overshoot - 0.12) < 1e-9, "easing survives");
        check(pos->keys[1].interp == core::Interpolation::Hold,
              "hold interpolation survives");
        check(pos->keys[1].time.mode == core::TimeMode::Beats,
              "a beat-authored keyframe stays in beats");
    }
    check(pos != nullptr && pos->expression.has_value(), "an expression survives");

    check(layer.effects.size() == 1, "the effect survives");
    check(layer.effects[0].schema == 1, "its schema version is recorded for migration");
    check(layer.effects[0].params.size() == 1, "its parameters survive");

    // Hand-placed markers are the one thing re-analysis must never destroy, so they
    // had better survive a save too.
    check(comp.rhythm.markers().size() == 3, "every marker survives");
    check(comp.rhythm.nearestIn(5.5, {core::MarkerLane::User}).has_value(),
          "the user marker survives with its lane intact");

    // A newly added layer must not collide with an id restored from the file.
    core::Project reopened;
    check(io::fromJson(reopened, io::toJson(original)).ok, "reopen for the id check");
    core::Composition& target = reopened.compositions().front();
    const core::LayerId existing = target.layers.front().id;
    core::Layer& added = reopened.addLayer(target, "new", core::LayerKind::Solid);
    check(added.id != existing, "ids minted after a load do not collide with loaded ones");

    // --- files that are wrong ---------------------------------------------------
    core::Project untouched = makeProject();
    const std::string before = io::toJson(untouched);

    check(!io::fromJson(untouched, "{ not json").ok, "garbage is rejected");
    check(!io::fromJson(untouched, R"({"format":"something-else"})").ok,
          "another app's JSON is rejected");
    check(!io::fromJson(untouched, R"({"format":"ruby-project","schema":9999})").ok,
          "a file from a newer Ruby is refused rather than silently downgraded");
    check(io::toJson(untouched) == before,
          "a failed load leaves the existing project untouched");

    // A missing pool entry should cost the link, not the layer.
    core::Project dangling;
    const std::string broken =
        R"({"format":"ruby-project","schema":1,"media":[],"compositions":[
             {"id":1,"name":"c","width":1080,"height":1920,"fps":30,"duration":10,
              "layers":[{"id":2,"name":"orphan","kind":"footage","media":77}]}]})";
    report = io::fromJson(dangling, broken);
    check(report.ok, "a project with a dangling media reference still opens");
    check(!report.notes.empty(), "and says what it had to fix");
    check(dangling.compositions().front().layers.size() == 1, "the layer is kept");
    check(!dangling.compositions().front().layers.front().media.has_value(),
          "with the broken link dropped");

    // Sparse files are what hand-editing produces.
    core::Project sparse;
    report = io::fromJson(
        sparse, R"({"format":"ruby-project","schema":1,"compositions":[{"name":"bare"}]})");
    check(report.ok, "a file missing most fields still opens");
    check(sparse.compositions().size() == 1, "and the composition is there");
    check(sparse.compositions().front().width > 0, "with defaults filled in");

    if (failures != 0) {
        std::fprintf(stderr, "\n%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("projectio: all checks passed");
    return EXIT_SUCCESS;
}
