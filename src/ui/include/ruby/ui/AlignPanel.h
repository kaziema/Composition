#pragma once

#include <QWidget>
#include <vector>

namespace ruby::ui {

// Snapping layers to the composition's edges and centres.
//
// This is the cheapest useful panel in the app: it reads where a layer landed, works out
// one number, and writes it back to Position. No render work, no new document state. The
// reason it earns a tab anyway is that the alternative is dragging with snapping on and
// hoping, and centring a title is something an editor does forty times a night.
//
// Ruby stores Position as a percentage of the frame, so "align left" is a percentage that
// still means "left" after the composition is resized. In a pixel-based app it is not.
class AlignPanel : public QWidget {
    Q_OBJECT

public:
    explicit AlignPanel(QWidget* parent = nullptr);

    enum class Align { Left, HCenter, Right, Top, VCenter, Bottom };

    // What layers line up against. Composition is the frame's edges and centre; Selection
    // is the bounding box of everything picked, which needs at least two of them.
    enum class Target { Composition, Selection };

    // How many alignable layers are selected.
    //
    // A count rather than a bool because the three states are genuinely different: none
    // means nothing works, one means align-to-composition only, and three or more is what
    // Distribute needs. AE greys its rows on exactly these thresholds and the reason is
    // arithmetic, not taste: two layers have nothing between them to space out.
    void setSelectionCount(int count);

    // Where a button sits. Public for the same reason PanelFrame::tabRect is: a row of
    // controls laid out at its natural width and never asked what room it had is a bug
    // this codebase has now shipped three times, and nothing outside could see it.
    [[nodiscard]] QRect buttonRect(int index) const;
    [[nodiscard]] int buttonCount() const;

signals:
    void alignRequested(Align edge, Target target);
    void distributeRequested(Align axis);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void resizeEvent(QResizeEvent* e) override;
    bool event(QEvent* e) override;

private:
    // One button. `align` is meaningless on a distribute button, which is drawn and does
    // nothing: distributing needs three layers selected and Ruby selects one at a time.
    struct Button {
        QRect rect;
        int glyph = 0;       // index into the glyph painter
        bool distribute = false;
        Align align = Align::Left;
        const char* tip = "";
    };

    void layoutButtons();
    [[nodiscard]] int buttonAt(const QPoint& pos) const;
    [[nodiscard]] bool enabled(const Button& b) const;

    std::vector<Button> buttons_;
    QRect targetRect_;  // the "Align Layers to:" dropdown
    int labelW_ = 90;  // measured, not assumed
    int hover_ = -1;
    int selectionCount_ = 0;
    Target target_ = Target::Composition;
};

}  // namespace ruby::ui
