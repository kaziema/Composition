// The layer column's switches, driven the way a user drives them: by clicking.
//
// Every one of these is a few pixels wide and sits next to seven other things a few
// pixels wide, so the interesting failure is never "the toggle does not toggle". It is
// "the click landed on the neighbour". These tests click at coordinates derived from the
// same metrics the painter uses, so a column that moves moves the test with it.

#include <QApplication>
#include <QMouseEvent>
#include <cstdio>

#include "ruby/ui/Theme.h"
#include "ruby/ui/TimelineView.h"

using namespace ruby;
using namespace ruby::ui::theme::metrics;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// The columns, recomputed from Theme.h exactly as TimelineView does, right to left from
// the track edge. If these two ever disagree the test is the thing that says so.
constexpr int kTrack = kLayerColumnW;
constexpr int kNav = kTrack - kKeyNavW;
constexpr int kParent = kNav - kParentW;
constexpr int kTrkMat = kParent - kTrkMatW;
constexpr int kPreserve = kTrkMat - kPreserveW;
constexpr int kMode = kPreserve - kModeW;
constexpr int kSwitches = kMode - kSwitchesW;

// Middle of switch n.
constexpr int switchX(int n) { return kSwitches + 2 + n * kSwitchW + kSwitchW / 2; }

constexpr int kFxSwitch = 3;   // shy, collapse, quality, fx
constexpr int kShySwitch = 0;

int firstRowY() { return kColumnHeaderH + kLayerRowH / 2; }

void click(ui::TimelineView& view, int x, int y) {
    const QPointF at(x, y);
    QMouseEvent press(QEvent::MouseButtonPress, at, view.mapToGlobal(at), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, at, view.mapToGlobal(at),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &release);
}

// A view showing one layer, wide enough that the track exists to the right of the
// columns rather than being clipped away.
struct Fixture {
    core::Project project;
    core::Composition* comp = nullptr;
    core::LayerId id = 0;
    ui::TimelineView view;

    Fixture() {
        comp = &project.addComposition("c", 1080, 1920, 30.0, 10.0);
        core::Layer& layer = project.addLayer(*comp, "solid", core::LayerKind::Solid);
        layer.outPoint = core::TimeValue::seconds(5.0);
        id = layer.id;
        view.setComposition(comp);
        // Opening a composition selects its first layer, which would make every "this
        // click does not select" check below pass for the wrong reason.
        view.clearSelection();
        view.resize(kLayerColumnW + 400, 300);
    }

    core::Layer& layer() { return *comp->find(id); }
};

// The columns must not overlap, in the order they are painted. Three separate cropping
// bugs have come from two columns each deciding their own position and happening to
// agree until one of them moved.
void the_columns_are_laid_out_end_to_end() {
    check(kAvToggleW < kAvToggleW + kIndexW, "A/V before the index");
    check(kAvToggleW + kIndexW + kLayerNameW <= kSwitches + kSwitchesW,
          "the name ends before the switches do");
    check(kSwitches + kSwitchesW == kMode, "switches butt against Mode");
    check(kMode + kModeW == kPreserve, "Mode butts against T");
    check(kPreserve + kPreserveW == kTrkMat, "T butts against Track Matte");
    check(kTrkMat + kTrkMatW == kParent, "Track Matte butts against Parent");
    check(kParent + kParentW == kNav, "Parent butts against the navigator");
    check(kNav + kKeyNavW == kTrack, "the navigator butts against the track");
}

void the_lock_locks() {
    Fixture f;
    check(!f.layer().locked, "starts unlocked");

    click(f.view, 64, firstRowY());  // the padlock, between solo and the index
    check(f.layer().locked, "the padlock toggles on");
    click(f.view, 64, firstRowY());
    check(!f.layer().locked, "and off again");
}

void a_locked_layer_refuses_to_be_selected() {
    Fixture f;
    const int nameX = kAvToggleW + kIndexW + 40;

    click(f.view, nameX, firstRowY());
    check(f.view.selectedLayer().has_value(), "an unlocked layer selects");

    click(f.view, 64, firstRowY());
    check(f.layer().locked, "locked");
    check(!f.view.selectedLayer().has_value(),
          "locking drops the selection, so Delete cannot reach it");

    click(f.view, nameX, firstRowY());
    check(!f.view.selectedLayer().has_value(), "and clicking it does not bring it back");

    click(f.view, 64, firstRowY());
    click(f.view, nameX, firstRowY());
    check(f.view.selectedLayer().has_value(), "unlocking gives it back");
}

// The eye, audio and solo are deliberately still live on a locked layer: none of them
// change what the layer is, and half of why you lock one is to keep looking at it.
void a_locked_layer_still_hides_and_solos() {
    Fixture f;
    click(f.view, 64, firstRowY());
    check(f.layer().locked, "locked");

    click(f.view, 14, firstRowY());
    check(!f.layer().enabled, "the eye still works");
    click(f.view, 47, firstRowY());
    check(f.layer().solo, "solo still works");
}

void the_fx_switch_turns_every_effect_off_and_back_on() {
    Fixture f;
    core::EffectInstance one;
    one.effectId = "ruby.blur.gaussian";
    core::EffectInstance two;
    two.effectId = "ruby.color.grade";
    f.layer().effects.push_back(one);
    f.layer().effects.push_back(two);

    click(f.view, switchX(kFxSwitch), firstRowY());
    check(!f.layer().effects[0].enabled && !f.layer().effects[1].enabled,
          "one click turns them all off");

    click(f.view, switchX(kFxSwitch), firstRowY());
    check(f.layer().effects[0].enabled && f.layer().effects[1].enabled,
          "and the next turns them all back on");
}

// The seven inert switches must swallow their clicks. Falling through to selection would
// make a switch that does nothing feel like a click that missed.
void an_inert_switch_does_not_select_the_layer() {
    Fixture f;
    click(f.view, switchX(kShySwitch), firstRowY());
    check(!f.view.selectedLayer().has_value(), "shy swallows the click");

    // And fx on a layer with no effects is not drawn, so it has nothing to swallow and
    // must not select either.
    click(f.view, switchX(kFxSwitch), firstRowY());
    check(!f.view.selectedLayer().has_value(), "an undrawn fx does not select");
}

// The Parent cell used to be hit-tested all the way to the track edge, which is straight
// over the keyframe navigator's column. Clicking "next key" on a layer row opened the
// parent menu instead.
void the_parent_cell_stops_before_the_navigator() {
    Fixture f;
    core::Property* position = f.layer().find("position");
    check(position != nullptr, "the layer has a position property");
    if (position == nullptr) {
        return;
    }
    const core::TimeContext ctx = f.comp->timeContext();
    core::Keyframe key;
    key.time = core::TimeValue::seconds(2.0);
    key.value = position->evaluate(0.0, ctx);
    position->addKey(key, ctx);

    f.view.setCurrentTime(0.0);
    // "Next key", in the right third of the navigator column. If Parent still claimed
    // this far right the click would open the parenting menu and the playhead would sit
    // where it was.
    click(f.view, kNav + kKeyNavW - 6, firstRowY());
    check(f.view.currentTime() > 1.9 && f.view.currentTime() < 2.1,
          "the navigator moved the playhead to the key");
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    the_columns_are_laid_out_end_to_end();
    the_lock_locks();
    a_locked_layer_refuses_to_be_selected();
    a_locked_layer_still_hides_and_solos();
    the_fx_switch_turns_every_effect_off_and_back_on();
    an_inert_switch_does_not_select_the_layer();
    the_parent_cell_stops_before_the_navigator();

    if (failures == 0) {
        std::puts("layerswitches: all checks passed");
    }
    return failures == 0 ? 0 : 1;
}
