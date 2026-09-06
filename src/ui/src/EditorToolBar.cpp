#include "comp/ui/EditorToolBar.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>

#include "comp/ui/Theme.h"

namespace comp::ui {

using namespace theme;

namespace {

// Handoff uses text glyphs as deliberate stand-ins for a real icon set:
// selection / pan / text / shape / pen / mask / hand / anchor / fx.
const QStringList kToolGlyphs = {
    QStringLiteral("▶"),  // selection
    QStringLiteral("✥"),  // pan behind
    QStringLiteral("T"),       // text
    QStringLiteral("◻"),  // shape
    QStringLiteral("✎"),  // pen
    QStringLiteral("⬔"),  // mask
    QStringLiteral("✋"),  // hand
    QStringLiteral("⌾"),  // anchor
    QStringLiteral("fx"),      // effects
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

    cacheText_ = QStringLiteral("RAM cached 0–12s");
    relayout();
}

void EditorToolBar::setCacheText(const QString& text) {
    cacheText_ = text;
    update();
}

void EditorToolBar::relayout() {
    const int cy = (metrics::kToolBarH - metrics::kToolButtonH) / 2;

    toolRects_.clear();
    int x = kEdgePad;
    for (int i = 0; i < kToolGlyphs.size(); ++i) {
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

        p.setPen(active ? QColor("#12212e") : kTextTertiary);
        p.drawText(r, Qt::AlignCenter, kToolGlyphs.at(i));
    }

    p.fillRect(dividerRect_, kDivider);

    for (const Switch& sw : switches_) {
        paintSwitch(p, sw);
    }

    // Right-aligned cache indicator: green dot + mono label.
    QFont mono = font();
    mono.setFamily(monoFontFamily());
    mono.setPixelSize(type::kMeta);
    p.setFont(mono);

    const QFontMetrics fm(mono);
    const int textW = fm.horizontalAdvance(cacheText_);
    const int textX = width() - kEdgePad - textW;

    p.setPen(kTextDim);
    p.drawText(QRect(textX, 0, textW, height()), Qt::AlignVCenter | Qt::AlignLeft, cacheText_);

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(kCacheReady);
    p.drawEllipse(QPointF(textX - 9.0, height() / 2.0), 3.0, 3.0);
    p.setRenderHint(QPainter::Antialiasing, false);

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

}  // namespace comp::ui
