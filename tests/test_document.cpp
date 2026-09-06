// Tests for the document model: beat map, layers, compositions, project.

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "comp/core/Document.h"

using namespace comp::core;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void checkNear(double a, double b, const char* what, double eps = 1e-9) {
    if (std::fabs(a - b) > eps) {
        std::fprintf(stderr, "FAIL: %s (got %.8f, want %.8f)\n", what, a, b);
        ++failures;
    }
}

BeatMap fourBars(double bpm) {
    // 16 beats at the given tempo, downbeat every 4.
    std::vector<Beat> beats;
    const double step = 60.0 / bpm;
    for (int i = 0; i < 16; ++i) {
        beats.push_back({static_cast<double>(i) * step, i, (i % 4) == 0});
    }
    return BeatMap(std::move(beats), bpm);
}

// The most important test in this file. Beat detection lives in the private module
// (D6), so in the public build the map is ALWAYS empty. Every query has to degrade
// to a sane no-op rather than throwing, returning garbage, or crashing.
void an_empty_beat_map_is_an_ordinary_state() {
    const BeatMap none;

    check(none.empty(), "a default beat map is empty");
    check(!none.nearest(3.0).has_value(), "nearest on an empty map returns nothing");
    check(!none.nearestDownbeat(3.0).has_value(), "nearestDownbeat on empty returns nothing");
    checkNear(none.snap(3.14159), 3.14159, "snapping against an empty map is a no-op");
    checkNear(none.snapToDownbeat(3.14159), 3.14159, "downbeat snap on empty is a no-op");
    check(none.downbeatsBetween(0.0, 12.0).empty(), "no cut points without a beat map");
}

void beats_are_sorted_on_construction() {
    BeatMap map({{2.0, 2, false}, {0.0, 0, true}, {1.0, 1, false}}, 120.0);
    check(map.beats().size() == 3, "all beats retained");
    checkNear(map.beats()[0].seconds, 0.0, "sorted first");
    checkNear(map.beats()[1].seconds, 1.0, "sorted second");
    checkNear(map.beats()[2].seconds, 2.0, "sorted third");
}

void snapping_picks_the_nearest_beat() {
    const BeatMap map = fourBars(120.0);  // beats every 0.5s

    checkNear(map.snap(0.6), 0.5, "0.6s snaps back to 0.5");
    checkNear(map.snap(0.9), 1.0, "0.9s snaps forward to 1.0");
    checkNear(map.snap(0.0), 0.0, "an exact beat stays put");
}

// "Cut on the downbeat, shake on the beat" is the grammar of these edits, so
// downbeat snapping has to ignore the beats in between.
void downbeat_snapping_ignores_ordinary_beats() {
    const BeatMap map = fourBars(120.0);  // downbeats at 0, 2, 4, 6s

    checkNear(map.snapToDownbeat(0.6), 0.0, "0.6s snaps to the downbeat at 0");
    checkNear(map.snapToDownbeat(1.7), 2.0, "1.7s snaps to the downbeat at 2");
    checkNear(map.snap(1.7), 1.5, "plain snap would have gone to 1.5 instead");
}

void cut_points_are_a_half_open_range() {
    const BeatMap map = fourBars(120.0);  // downbeats at 0, 2, 4, 6
    const std::vector<double> cuts = map.downbeatsBetween(0.0, 4.0);

    check(cuts.size() == 2, "0..4 contains two downbeats");
    checkNear(cuts[0], 0.0, "range includes its start");
    checkNear(cuts[1], 2.0, "second cut point");
    check(map.downbeatsBetween(0.0, 0.0).empty(), "an empty range yields no cuts");
}

void layer_labels_follow_the_design() {
    check(defaultLabelFor(LayerKind::Text) == LabelColor::Lavender, "text is lavender");
    check(defaultLabelFor(LayerKind::Shape) == LabelColor::Lavender, "shape is lavender");
    check(defaultLabelFor(LayerKind::Precomp) == LabelColor::Aqua, "precomp is aqua");
    check(defaultLabelFor(LayerKind::Audio) == LabelColor::Green, "audio is green");
    check(defaultLabelFor(LayerKind::Footage) == LabelColor::Gray, "footage is gray");
}

// Position stored in pixels is why an AE preset breaks when the comp is reshaped.
// Storing it as a percentage is the fix, so the default transform must declare it.
void the_default_transform_stores_resolution_independent_units() {
    const std::vector<Property> t = defaultTransform();
    check(t.size() == 5, "five transform properties");

    const auto findKey = [&t](const char* key) -> const Property* {
        for (const Property& p : t) {
            if (p.key == key) {
                return &p;
            }
        }
        return nullptr;
    };

    const Property* pos = findKey("position");
    check(pos != nullptr, "position exists");
    check(pos != nullptr && pos->unit != SpatialUnit::Px,
          "position is not stored in pixels");

    const Property* rot = findKey("rotation");
    check(rot != nullptr && rot->unit == SpatialUnit::Degrees, "rotation is in degrees");

    const Property* op = findKey("opacity");
    check(op != nullptr && op->staticValue.x() == 100.0, "opacity defaults to 100");
}

void new_layers_land_on_top() {
    Project project;
    Composition& comp = project.addComposition("sneaker_drop", 1080, 1920, 30.0, 12.0);

    project.addLayer(comp, "footage", LayerKind::Footage);
    project.addLayer(comp, "captions", LayerKind::Precomp);
    Layer& newest = project.addLayer(comp, "DROP 09.12", LayerKind::Text);

    check(comp.layers.size() == 3, "three layers");
    check(comp.layers.front().name == "DROP 09.12", "the newest layer is topmost, as in AE");
    check(newest.label == LabelColor::Lavender, "a text layer gets the lavender label");
    check(newest.find("position") != nullptr, "a new layer has its transform stack");
    check(newest.find("nonexistent") == nullptr, "looking up an unknown key returns null");
}

void keyframe_counts_roll_up() {
    Project project;
    Composition& comp = project.addComposition("c", 1080, 1920, 30.0, 12.0);
    Layer& layer = project.addLayer(comp, "text", LayerKind::Text);

    const TimeContext ctx = comp.timeContext();
    Property* pos = layer.find("position");
    check(pos != nullptr, "position found");
    if (pos != nullptr) {
        pos->addKey({TimeValue::seconds(0.6), Value::vec2(0, 0), Interpolation::Bezier, 0, 0, 0},
                    ctx);
        pos->addKey({TimeValue::seconds(1.3), Value::vec2(1, 1), Interpolation::Bezier, 0, 0, 0},
                    ctx);
    }

    check(comp.layers.front().keyframeCount() == 2, "layer counts its keyframes");
    check(comp.totalKeyframes() == 2, "composition rolls up layer keyframes");
}

// In the public build there is no detector, so `beats` mode still has to resolve.
void time_context_falls_back_without_a_beat_map() {
    Project project;
    Composition& comp = project.addComposition("c", 1080, 1920, 30.0, 12.0);

    TimeContext ctx = comp.timeContext();
    check(!ctx.has_beat_map, "a fresh comp has no beat map");
    checkNear(ctx.bpm, 120.0, "falls back to a nominal tempo so beats still resolve");
    checkNear(to_seconds(TimeValue::beats(4.0), ctx), 2.0, "one bar at the fallback tempo");

    comp.beats = fourBars(174.0);
    ctx = comp.timeContext();
    check(ctx.has_beat_map, "analysis makes the map present");
    checkNear(ctx.bpm, 174.0, "tempo now comes from the map");
}

}  // namespace

int main() {
    an_empty_beat_map_is_an_ordinary_state();
    beats_are_sorted_on_construction();
    snapping_picks_the_nearest_beat();
    downbeat_snapping_ignores_ordinary_beats();
    cut_points_are_a_half_open_range();
    layer_labels_follow_the_design();
    the_default_transform_stores_resolution_independent_units();
    new_layers_land_on_top();
    keyframe_counts_roll_up();
    time_context_falls_back_without_a_beat_map();

    if (failures != 0) {
        std::fprintf(stderr, "\n%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("document: all checks passed");
    return EXIT_SUCCESS;
}
