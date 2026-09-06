#pragma once

#include <QList>
#include <QRect>
#include <QWidget>

namespace comp::ui {

// The 30px tool bar: nine 24x22 tool buttons, a divider, two labeled switches
// (Snapping, Motion Blur), and a right-aligned RAM cache indicator.
//
// Custom-painted rather than assembled from QToolButtons, because the handoff
// pins exact sizes and the switch is a bespoke 26x13 pill with a 9px knob.
class EditorToolBar : public QWidget {
    Q_OBJECT

public:
    explicit EditorToolBar(QWidget* parent = nullptr);

    void setCacheText(const QString& text);

signals:
    void toolSelected(int index);
    void snappingToggled(bool on);
    void motionBlurToggled(bool on);

protected:
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
    QString cacheText_;
    int activeTool_ = 0;
    int hoverTool_ = -1;
};

}  // namespace comp::ui
