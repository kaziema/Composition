// The align panel's layout.
//
// Six buttons in a row inside a splitter with no minimum width. This codebase has now
// shipped the same bug three times: a row of controls laid out at its natural width,
// never asked what room it had, painted off the right edge and unclickable. The tab strip
// had it, the toolbar had it, this had it. So it gets the same check they do.

#include <QApplication>
#include <cstdio>

#include "ruby/ui/AlignPanel.h"

using namespace ruby;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Every button lands inside the panel, at every width the panel can be dragged to.
void the_buttons_stay_inside_the_panel() {
    ui::AlignPanel panel;
    panel.show();

    for (const int w : {268, 220, 180, 150, 120}) {
        panel.resize(w, 300);
        QApplication::processEvents();

        for (int i = 0; i < panel.buttonCount(); ++i) {
            const QRect r = panel.buttonRect(i);
            check(r.width() > 0, "a button with no width is not a button");
            check(r.left() >= 0, "no button starts off the left edge");
            if (r.right() >= w) {
                std::fprintf(stderr, "  at width %d, button %d ends at %d\n", w, i,
                             r.right());
                check(false, "a button ran off the right edge");
            }
        }
    }
    panel.hide();
}

// The two rows line up with each other. They are the same six columns and reading them as
// pairs is the whole point of the panel.
void the_two_rows_share_their_columns() {
    ui::AlignPanel panel;
    panel.show();
    panel.resize(200, 300);
    QApplication::processEvents();

    for (int i = 0; i < 6; ++i) {
        check(panel.buttonRect(i).left() == panel.buttonRect(i + 6).left(),
              "align and distribute share a column");
    }
    panel.hide();
}

// Buttons do not overlap each other, in either row.
void the_buttons_do_not_overlap() {
    ui::AlignPanel panel;
    panel.show();
    panel.resize(140, 300);
    QApplication::processEvents();

    for (int i = 0; i < 5; ++i) {
        check(panel.buttonRect(i).right() < panel.buttonRect(i + 1).left(),
              "align buttons keep off each other");
        check(panel.buttonRect(i + 6).right() < panel.buttonRect(i + 7).left(),
              "distribute buttons keep off each other");
    }
    panel.hide();
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    the_buttons_stay_inside_the_panel();
    the_two_rows_share_their_columns();
    the_buttons_do_not_overlap();

    if (failures == 0) {
        std::puts("alignpanel: all checks passed");
    }
    return failures == 0 ? 0 : 1;
}
