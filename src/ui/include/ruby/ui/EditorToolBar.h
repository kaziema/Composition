#pragma once

#include <QList>
#include <QRect>
#include <QWidget>

namespace ruby::ui {

// The 30px tool bar: nine 24x22 tool buttons, a divider, and two labelled switches
// (Snapping, Motion Blur). Machine readouts live in the status bar instead: this bar is
// for things you act on.
//
// Custom-painted rather than assembled from QToolButtons: the sizes are exact and the
// switch is a bespoke 26x13 pill with a 9px knob.
class EditorToolBar : public QWidget {
    Q_OBJECT

public:
    explicit EditorToolBar(QWidget* parent = nullptr);

signals:
    void toolSelected(int index);
    void snappingToggled(bool on);
    void motionBlurToggled(bool on);

protected:
    bool event(QEvent* e) override;
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    struct Switch {
        QString label;
        bool on;
        QRect labelRect;
        QRect pillRect;
    };

    void relayout();
    void paintSwitch(QPainter& p, const Switch& sw) const;

    QList<QRect> toolRects_;
    QList<Switch> switches_;
    QRect dividerRect_;
    int activeTool_ = 0;
    int hoverTool_ = -1;
};

}  // namespace ruby::ui
