// Regression test for the twirl arrow showing nothing.
//
// Opening a layer's disclosure arrow used to reveal only properties that already had
// keyframes, so a fresh layer twirled open to an empty space and there was no way to
// reach Position or Scale from the timeline at all. Kaz hit this trying to look at a
// layer's transform.
//
// Row heights are the measurable thing from outside the class, so the test works in the
// currency of contentHeight rather than reaching into the row list.

#include <QApplication>
#include <cstdio>
#include <cstdlib>

#include "ruby/ui/TimelineView.h"
#include "ruby/ui/Theme.h"

using namespace ruby;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    core::Project project;
    core::Composition& comp = project.addComposition("c", 1080, 1920, 30.0, 10.0);
    const core::LayerId id = project.addLayer(comp, "solid", core::LayerKind::Solid).id;

    ui::TimelineView view;
    view.resize(1200, 600);
    view.setComposition(&comp);

    const int closed = view.contentHeight();
    check(closed > 0, "a closed layer has some height");

    // A layer with NO keyframes at all: the case that used to open to nothing.
    view.toggleExpanded(id);
    const int opened = view.contentHeight();
    check(opened > closed,
          "twirling a layer with no keyframes still reveals its transform properties");

    // One group header plus five transform properties: anchor point, position, scale,
    // rotation, opacity.
    const int expected = closed + ui::theme::metrics::kPropertyRowH * 6;
    check(opened == expected, "a Transform header and all five transform properties");

    view.toggleExpanded(id);
    check(view.contentHeight() == closed, "and closing it puts the height back");

    // U is a different thing: open, but only what is animated. With nothing animated that
    // is the header and nothing else.
    view.revealAnimated(id);
    const int revealed = view.contentHeight();
    check(revealed < opened, "U shows less than the twirl arrow does");
    check(revealed > closed, "but still opens the layer");

    // Pressing U again closes it, the way AE's does.
    view.revealAnimated(id);
    check(view.contentHeight() == closed, "U toggles closed again");

    // With something animated, U shows that one property.
    {
        const core::TimeContext ctx = comp.timeContext();
        core::Property* scale = comp.find(id)->find("scale");
        scale->addKey({core::TimeValue::seconds(0.0), core::Value::vec2(100.0, 100.0),
                       core::Interpolation::Linear, 0.0, 0.0, 0.0}, ctx);
        scale->addKey({core::TimeValue::seconds(1.0), core::Value::vec2(50.0, 50.0),
                       core::Interpolation::Linear, 0.0, 0.0, 0.0}, ctx);
    }
    view.setComposition(&comp);
    view.revealAnimated(id);
    check(view.contentHeight() == closed + ui::theme::metrics::kPropertyRowH * 2,
          "U shows the header and only the animated property");

    // And the twirl arrow, on a layer U had filtered, goes back to showing everything.
    view.toggleExpanded(id);   // closes
    view.toggleExpanded(id);   // opens, unfiltered
    check(view.contentHeight() == expected,
          "the arrow always means everything, even after U filtered it");

    if (failures != 0) {
        std::fprintf(stderr, "\n%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("timelinerows: all checks passed");
    return EXIT_SUCCESS;
}
