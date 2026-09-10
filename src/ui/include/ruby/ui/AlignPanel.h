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

    // Whether anything is selected that can be aligned. Audio layers cannot: they have no
    // picture, so there is no edge to line up.
    void setAlignable(bool alignable);

signals:
    void alignRequested(Align edge);

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
    int hover_ = -1;
    bool alignable_ = false;
};

}  // namespace ruby::ui
