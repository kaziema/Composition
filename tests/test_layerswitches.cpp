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

// The group twirl's column, matching TimelineView::groupTwirlLeft().
constexpr int kGroupTwirl = 22 + 6;

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

// Group headers collapse. Height is the measurable thing from outside the class, the
// same currency test_timelinerows works in.
void a_group_collapses_and_reopens() {
    Fixture f;
    f.view.toggleExpanded(f.id);

    const int openHeight = f.view.contentHeight();
    check(openHeight > kColumnHeaderH + kLayerRowH,
          "the layer opens onto its transform rows");

    // The group twirl, one indent step in from the layer's own. The Transform header is
    // the first row under the layer.
    const int transformRowY = kColumnHeaderH + kLayerRowH + kPropertyRowH / 2;
    click(f.view, kGroupTwirl, transformRowY);

    const int shutHeight = f.view.contentHeight();
    check(shutHeight < openHeight, "shutting Transform takes its rows away");
    check(shutHeight == kColumnHeaderH + kLayerRowH + kPropertyRowH,
          "and leaves exactly the layer and the group header");

    click(f.view, kGroupTwirl, transformRowY);
    check(f.view.contentHeight() == openHeight, "and opening it gives them back");
}

// The group header is a label everywhere except the twirl. Clicking the word "Transform"
// must not shut it, because the same row is the right-click target for an effect.
void the_group_label_is_not_a_button() {
    Fixture f;
    f.view.toggleExpanded(f.id);
    const int openHeight = f.view.contentHeight();

    const int transformRowY = kColumnHeaderH + kLayerRowH + kPropertyRowH / 2;
    click(f.view, kGroupTwirl + 60, transformRowY);
    check(f.view.contentHeight() == openHeight, "clicking the label changes nothing");
}

// A shut group must not swallow the layer twirl or leave the layer selected by accident.
void shutting_a_group_does_not_select_the_layer() {
    Fixture f;
    f.view.toggleExpanded(f.id);
    const int transformRowY = kColumnHeaderH + kLayerRowH + kPropertyRowH / 2;
    click(f.view, kGroupTwirl, transformRowY);
    check(!f.view.selectedLayer().has_value(), "the twirl is not a selection");
}

// The lock has to hold against every way of selecting a layer, not only a left click on
// its name. The right-click menu selects the row it was opened on before showing itself,
// and every layer command in the window acts on the selection, so a locked layer that
// could be selected that way could still be deleted.
void nothing_selects_a_locked_layer() {
    Fixture f;
    click(f.view, 64, firstRowY());
    check(f.layer().locked, "locked");

    f.view.selectLayer(f.id);
    check(!f.view.selectedLayer().has_value(),
          "selectLayer refuses it, so the context menu cannot get in that way either");

    f.layer().locked = false;
    f.view.selectLayer(f.id);
    check(f.view.selectedLayer().has_value(), "and takes it once the padlock is open");
}

// Preserve Transparency and Track Matte are drawn as controls and do nothing yet. Like
// the seven inert switches, they must swallow the click rather than selecting the layer.
void the_inert_mode_cells_swallow_their_clicks() {
    Fixture f;
    const int preserve = kPreserve + kPreserveW / 2;
    const int trkMat = kTrkMat + kTrkMatW / 2;

    click(f.view, preserve, firstRowY());
    check(!f.view.selectedLayer().has_value(), "T swallows the click");

    click(f.view, trkMat, firstRowY());
    check(!f.view.selectedLayer().has_value(), "Track Matte swallows the click");

    // Not tested here: the Mode cell next door. It opens a blocking menu, so clicking it
    // from a test hangs the test rather than failing it.
}

// --- multi-layer selection ---------------------------------------------------

// A fixture with a stack, so range selection has something to range over.
struct Stack {
    core::Project project;
    core::Composition* comp = nullptr;
    std::vector<core::LayerId> ids;
    ui::TimelineView view;

    explicit Stack(int count) {
        comp = &project.addComposition("c", 1080, 1920, 30.0, 10.0);
        for (int i = 0; i < count; ++i) {
            const core::LayerId id =
                project.addLayer(*comp, "layer", core::LayerKind::Solid).id;
            comp->find(id)->outPoint = core::TimeValue::seconds(5.0);
            ids.push_back(id);
        }
        // Topmost first, so ids.front() is the BOTTOM of the stack: addLayer puts each new
        // one on top. Worth stating because a range test that assumes otherwise passes for
        // the wrong reason.
        view.setComposition(comp);
        view.clearSelection();
        view.resize(kLayerColumnW + 400, 400);
    }

    core::Layer& layer(core::LayerId id) { return *comp->find(id); }
    [[nodiscard]] std::size_t count() const { return view.selectedLayers().size(); }
};

using SelectMode = ui::TimelineView::SelectMode;

void a_plain_click_replaces_the_selection() {
    Stack s(3);
    s.view.selectLayer(s.ids[0], SelectMode::Replace);
    s.view.selectLayer(s.ids[1], SelectMode::Replace);
    check(s.count() == 1, "one at a time");
    check(s.view.selectedLayer() == s.ids[1], "and it is the one just clicked");
}

void toggling_adds_and_removes() {
    Stack s(3);
    s.view.selectLayer(s.ids[0], SelectMode::Replace);
    s.view.selectLayer(s.ids[1], SelectMode::Toggle);
    s.view.selectLayer(s.ids[2], SelectMode::Toggle);
    check(s.count() == 3, "three selected");
    check(s.view.selectedLayer() == s.ids[2], "the last one added is the primary");

    s.view.selectLayer(s.ids[1], SelectMode::Toggle);
    check(s.count() == 2, "toggling one out leaves the rest");
    check(!s.view.isSelected(s.ids[1]), "and it is the right one that left");
}

// Range works by row order, not by id. Ids are creation order, and a layer dragged up the
// stack would otherwise select a run that does not match what the user is pointing at.
void a_range_follows_the_stack_not_the_ids() {
    Stack s(5);
    s.view.selectLayer(s.comp->layers[0].id, SelectMode::Replace);
    s.view.selectLayer(s.comp->layers[3].id, SelectMode::Range);
    check(s.count() == 4, "four rows between them inclusive");
    for (int i = 0; i <= 3; ++i) {
        check(s.view.isSelected(s.comp->layers[static_cast<std::size_t>(i)].id),
              "every row in the run is selected");
    }
    check(!s.view.isSelected(s.comp->layers[4].id), "and nothing beyond it");
}

// Shift-clicking again re-extends from the same end rather than pivoting around wherever
// the range last finished. Dragging a range out and then shortening it is one gesture.
void the_range_anchor_stays_put() {
    Stack s(5);
    s.view.selectLayer(s.comp->layers[0].id, SelectMode::Replace);
    s.view.selectLayer(s.comp->layers[4].id, SelectMode::Range);
    check(s.count() == 5, "all five");

    s.view.selectLayer(s.comp->layers[2].id, SelectMode::Range);
    check(s.count() == 3, "shortened from the same anchor, not re-anchored at 4");
    check(s.view.isSelected(s.comp->layers[0].id), "the anchor is still in");
}

// A locked layer refuses selection however it is asked, including from inside a range.
// Every layer command works off the selection, so this is what the padlock means.
void a_range_steps_over_locked_layers() {
    Stack s(4);
    s.layer(s.comp->layers[1].id).locked = true;

    s.view.selectLayer(s.comp->layers[0].id, SelectMode::Replace);
    s.view.selectLayer(s.comp->layers[3].id, SelectMode::Range);
    check(s.count() == 3, "the locked one is not in the run");
    check(!s.view.isSelected(s.comp->layers[1].id), "specifically that one");

    s.view.selectLayer(s.comp->layers[1].id, SelectMode::Toggle);
    check(!s.view.isSelected(s.comp->layers[1].id), "and toggling cannot let it in either");
}

// Locking a layer takes it OUT of the selection rather than clearing the whole thing.
void locking_one_of_several_leaves_the_rest() {
    Stack s(3);
    s.view.selectLayer(s.ids[0], SelectMode::Replace);
    s.view.selectLayer(s.ids[1], SelectMode::Toggle);
    s.view.selectLayer(s.ids[2], SelectMode::Toggle);

    click(s.view, 64, kColumnHeaderH + kLayerRowH / 2);  // padlock on the top row
    check(s.comp->layers[0].locked, "locked");
    check(s.count() == 2, "the other two are still selected");
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
    a_group_collapses_and_reopens();
    the_group_label_is_not_a_button();
    shutting_a_group_does_not_select_the_layer();
    nothing_selects_a_locked_layer();
    the_inert_mode_cells_swallow_their_clicks();
    a_plain_click_replaces_the_selection();
    toggling_adds_and_removes();
    a_range_follows_the_stack_not_the_ids();
    the_range_anchor_stays_put();
    a_range_steps_over_locked_layers();
    locking_one_of_several_leaves_the_rest();

    if (failures == 0) {
        std::puts("layerswitches: all checks passed");
    }
    return failures == 0 ? 0 : 1;
}
