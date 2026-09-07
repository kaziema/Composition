#include "ruby/ui/EditorToolBar.h"

#include <QFontMetrics>
#include <iterator>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QToolTip>
#include <QPainter>

#include "ruby/ui/Theme.h"
#include "ruby/ui/ToolIcons.h"

namespace ruby::ui {

using namespace theme;

namespace {

// The nine tools, drawn as vector paths rather than text glyphs. Several of those
// glyphs carry Unicode emoji presentation, so the hand rendered in full colour and
// broke the tool bar's monochrome run.
constexpr ToolIcon kTools[] = {
    ToolIcon::Selection, ToolIcon::Pan,    ToolIcon::Text,
    ToolIcon::Shape,     ToolIcon::Pen,    ToolIcon::Mask,
    ToolIcon::Hand,      ToolIcon::Anchor, ToolIcon::Effects,
};
constexpr int kToolCount = static_cast<int>(std::size(kTools));

// Name plus what it does. A bare name on an abstract glyph tells you what it is called,
// not what it is for, and "Pan Behind" is the classic example of a name that explains
// nothing to someone who has not already been taught it.
constexpr const char* kToolTips[] = {
    "Selection Tool — pick and transform layers in the viewer",
    "Move Tool — reposition the selected layer",
    "Type Tool — create and edit text layers",
    "Shape Tool — draw rectangles, ellipses and polygons",
    "Pen Tool — draw bezier paths and masks by hand",
    "Mask Tool — mask a layer to a shape",
    "Hand Tool — pan the viewer without moving anything",
    "Anchor Point Tool — move a layer's origin without moving the layer",
    "Effects — browse and apply effects",
};

constexpr int kEdgePad = 8;
constexpr int kToolGap = 1;
constexpr int kSwitchPillW = 26;
constexpr int kSwitchPillH = 13;
constexpr int kSwitchKnob = 9;

}  // namespace

EditorToolBar::EditorToolBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(metrics::kToolBarH);
    setMouseTracking(true);

    QFont f = font();
    f.setPixelSize(type::kTabLabel);
    setFont(f);

    switches_ = {
        {QStringLiteral("Snapping"), true, {}, {}},
        {QStringLiteral("Motion Blur"), false, {}, {}},
    };

    relayout();
}

void EditorToolBar::relayout() {
    const int cy = (metrics::kToolBarH - metrics::kToolButtonH) / 2;

    toolRects_.clear();
    int x = kEdgePad;
    for (int i = 0; i < kToolCount; ++i) {
        toolRects_.append(QRect(x, cy, metrics::kToolButtonW, metrics::kToolButtonH));
        x += metrics::kToolButtonW + kToolGap;
    }

    x += 7;
    dividerRect_ = QRect(x, 6, 1, metrics::kToolBarH - 12);
    x += 1 + 11;

    const QFontMetrics fm(font());
    for (Switch& sw : switches_) {
        const int labelW = fm.horizontalAdvance(sw.label);
        sw.labelRect = QRect(x, 0, labelW, metrics::kToolBarH);
        x += labelW + 7;
        sw.pillRect = QRect(x, (metrics::kToolBarH - kSwitchPillH) / 2, kSwitchPillW,
                            kSwitchPillH);
        x += kSwitchPillW + 16;
    }
}

void EditorToolBar::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    relayout();
}

void EditorToolBar::paintSwitch(QPainter& p, const Switch& sw) const {
    p.setPen(sw.on ? kTextSecondary : kTextDim);
    p.drawText(sw.labelRect, Qt::AlignVCenter | Qt::AlignLeft, sw.label);

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(sw.on ? kAccent : QColor("#444444"));
    const qreal r = sw.pillRect.height() / 2.0;
    p.drawRoundedRect(sw.pillRect, r, r);

    p.setBrush(sw.on ? QColor("#ffffff") : kTextDim);
    const int knobY = sw.pillRect.top() + (sw.pillRect.height() - kSwitchKnob) / 2;
    const int knobX = sw.on ? sw.pillRect.right() - kSwitchKnob - 2 : sw.pillRect.left() + 2;
    p.drawEllipse(QRect(knobX, knobY, kSwitchKnob, kSwitchKnob));
    p.setRenderHint(QPainter::Antialiasing, false);
}

void EditorToolBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), kToolBar);

    for (int i = 0; i < toolRects_.size(); ++i) {
        const QRect r = toolRects_.at(i);
        const bool active = (i == activeTool_);

        if (active) {
            p.fillRect(r, kAccent);
        } else if (i == hoverTool_) {
            p.fillRect(r, kMenuActive);
        }

        paintToolIcon(p, r, kTools[i], active ? QColor("#12212e") : kTextTertiary);
    }

    p.fillRect(dividerRect_, kDivider);

    for (const Switch& sw : switches_) {
        paintSwitch(p, sw);
    }

    // Machine and engine readouts live in the status bar now, not up here. This bar is
    // for things you act on; that is for things you watch.
    p.setPen(kDivider);
    p.drawLine(0, height() - 1, width(), height() - 1);
}

void EditorToolBar::mousePressEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();

    for (int i = 0; i < toolRects_.size(); ++i) {
        if (toolRects_.at(i).contains(pos)) {
            if (i != activeTool_) {
                activeTool_ = i;
                update();
                emit toolSelected(i);
            }
            return;
        }
    }

    for (int i = 0; i < switches_.size(); ++i) {
        Switch& sw = switches_[i];
        if (sw.pillRect.contains(pos) || sw.labelRect.contains(pos)) {
            sw.on = !sw.on;
            update();
            if (i == 0) {
                emit snappingToggled(sw.on);
            } else {
                emit motionBlurToggled(sw.on);
            }
            return;
        }
    }
}

bool EditorToolBar::event(QEvent* e) {
    // The whole bar is one widget, so tooltips are resolved by hit-testing rather than
    // by having a child per control.
    if (e->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(e);
        const QPoint pos = help->pos();

        for (int i = 0; i < toolRects_.size() && i < kToolCount; ++i) {
            if (toolRects_.at(i).contains(pos)) {
                QToolTip::showText(help->globalPos(),
                                   QString::fromUtf8(kToolTips[i]), this);
                return true;
            }
        }
        for (const Switch& sw : switches_) {
            if (!sw.pillRect.contains(pos) && !sw.labelRect.contains(pos)) {
                continue;
            }
            const QString text =
                sw.label == QStringLiteral("Snapping")
                    ? QStringLiteral("Snapping — layer edges, the playhead and rhythm "
                                     "markers pull toward each other while dragging")
                    : QStringLiteral("Motion Blur — blur layers along their movement "
                                     "between frames");
            QToolTip::showText(help->globalPos(), text, this);
            return true;
        }

        QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

void EditorToolBar::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    int hit = -1;
    for (int i = 0; i < toolRects_.size(); ++i) {
        if (toolRects_.at(i).contains(pos)) {
            hit = i;
            break;
        }
    }
    if (hit != hoverTool_) {
        hoverTool_ = hit;
        update();
    }
}

void EditorToolBar::leaveEvent(QEvent*) {
    if (hoverTool_ != -1) {
        hoverTool_ = -1;
        update();
    }
}

}  // namespace ruby::ui
