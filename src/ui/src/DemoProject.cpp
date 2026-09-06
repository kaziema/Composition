#include "comp/ui/DemoProject.h"

namespace comp::ui::demo {

using namespace core;

namespace {

// The handoff lists keyframe positions in seconds against a 12 second window.
void keyAt(Property& p, const TimeContext& ctx, std::initializer_list<double> times) {
    double v = 0.0;
    for (const double t : times) {
        Keyframe k;
        k.time = TimeValue::seconds(t);
        k.value = Value::scalar(v);
        k.interp = Interpolation::Bezier;
        k.easeOut = 0.68;
        k.easeIn = 0.68;
        p.addKey(k, ctx);
        v += 10.0;
    }
}

Property makeProp(std::string key, std::string label, SpatialUnit unit, Value initial) {
    Property p;
    p.key = std::move(key);
    p.label = std::move(label);
    p.unit = unit;
    p.staticValue = initial;
    return p;
}

}  // namespace

Project sampleProject() {
    Project project;
    Composition& comp = project.addComposition("sneaker_drop_v4", 1080, 1920, 30.0, 12.0);

    const TimeContext ctx = comp.timeContext();

    // Added bottom-up, because addLayer puts each new layer on top.
    Layer& audio = project.addLayer(comp, "track_hardwave.wav", LayerKind::Audio);
    audio.inPoint = TimeValue::seconds(0.0);
    audio.outPoint = TimeValue::seconds(12.0);

    Layer& broll = project.addLayer(comp, "b-roll_street.mp4", LayerKind::Footage);
    broll.inPoint = TimeValue::seconds(6.8);
    broll.outPoint = TimeValue::seconds(12.0);

    Layer& sneaker = project.addLayer(comp, "sneaker_a4.mp4", LayerKind::Footage);
    sneaker.inPoint = TimeValue::seconds(0.0);
    sneaker.outPoint = TimeValue::seconds(7.2);

    Layer& captions = project.addLayer(comp, "captions", LayerKind::Precomp);
    captions.inPoint = TimeValue::seconds(1.2);
    captions.outPoint = TimeValue::seconds(10.6);
    captions.expanded = true;
    {
        Property hits = makeProp("word_pop", "word_pop", SpatialUnit::Normalized,
                                 Value::scalar(8.0));
        keyAt(hits, ctx, {1.4, 2.3, 3.1, 4.2, 5.4, 6.6, 8.0, 9.4});
        captions.properties.push_back(std::move(hits));
    }

    Layer& wipe = project.addLayer(comp, "swipe wipe", LayerKind::Shape);
    wipe.inPoint = TimeValue::seconds(3.9);
    wipe.outPoint = TimeValue::seconds(5.3);
    wipe.blend = BlendMode::Add;
    {
        Property* pos = wipe.find("position");
        if (pos != nullptr) {
            keyAt(*pos, ctx, {4.0, 4.6, 5.2});
        }
    }

    Layer& title = project.addLayer(comp, "DROP 09.12", LayerKind::Text);
    title.inPoint = TimeValue::seconds(0.4);
    title.outPoint = TimeValue::seconds(5.0);
    title.expanded = true;
    {
        Property* pos = title.find("position");
        if (pos != nullptr) {
            keyAt(*pos, ctx, {0.6, 1.3, 2.66, 4.4});
            pos->staticValue = Value::vec2(60.0, 812.0);
        }
        Property* scale = title.find("scale");
        if (scale != nullptr) {
            keyAt(*scale, ctx, {0.6, 1.1, 2.66});
        }
        Property tracking = makeProp("tracking", "Tracking", SpatialUnit::Normalized,
                                     Value::scalar(12.0));
        keyAt(tracking, ctx, {0.6, 2.0});
        title.properties.push_back(std::move(tracking));
    }

    return project;
}

}  // namespace comp::ui::demo
