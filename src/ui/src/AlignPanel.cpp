#include "ruby/ui/AlignPanel.h"

#include <QHelpEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>

#include "ruby/ui/Theme.h"

namespace ruby::ui {

using namespace theme;

namespace {

constexpr int kPad = 10;
constexpr int kRowGap = 8;
constexpr int kBtnW = 30;
constexpr int kBtnH = 24;
constexpr int kBtnGap = 4;
constexpr int kLabelH = 18;
constexpr int kTargetH = 22;

// The six align glyphs, then the six distribute ones, in the order they are laid out.
//
// Drawn from rectangles rather than a font: they are diagrams, not characters, and at
// 30x24 a diagram made of three rectangles is legible in a way a glyph is not.
void drawAlignGlyph(QPainter& p, int glyph, const QRect& box, const QColor& ink) {
    const double cx = box.center().x() + 0.5;
    const double cy = box.center().y() + 0.5;
    const bool vertical = glyph >= 3;  // top, vcenter, bottom work on the other axis
    const int edge = glyph % 3;        // 0 near, 1 centre, 2 far

    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(ink);

    // The rule everything lines up against, drawn brighter than the layers.
    const double rule = -6.0 + edge * 6.0;  // -6 near edge, 0 centre, +6 far edge
    if (vertical) {
        p.drawRect(QRectF(cx - 7.0, cy + rule - 0.5, 14.0, 1.0));
    } else {
        p.drawRect(QRectF(cx + rule - 0.5, cy - 7.0, 1.0, 14.0));
    }

    // Two layers of different sizes, so which edge is being lined up is unambiguous. Two
    // identical rectangles would look the same aligned left, centred and right.
    QColor bar = ink;
    bar.setAlphaF(ink.alphaF() * 0.62F);
    p.setBrush(bar);

    const double lengths[2] = {9.0, 5.0};
    for (int i = 0; i < 2; ++i) {
        const double len = lengths[i];
        const double off = (i == 0) ? -3.0 : 2.0;  // stacked either side of the middle
        if (vertical) {
            const double y = (edge == 0) ? rule + 1.0 : (edge == 2) ? rule - len - 1.0
                                                                    : rule - len / 2.0;
            p.drawRect(QRectF(cx + off - 1.5, cy + y, 3.0, len));
        } else {
            const double x = (edge == 0) ? rule + 1.0 : (edge == 2) ? rule - len - 1.0
                                                                    : rule - len / 2.0;
            p.drawRect(QRectF(cx + x, cy + off - 1.5, len, 3.0));
        }
    }
    p.restore();
}

void drawDistributeGlyph(QPainter& p, int glyph, const QRect& box, const QColor& ink) {
    const double cx = box.center().x() + 0.5;
    const double cy = box.center().y() + 0.5;
    const bool vertical = glyph >= 3;

    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(ink);
    // Three bars, evenly spaced. Which edge the spacing is measured from is the only
    // difference between the three in each direction, and at this size that difference is
    // not drawable, so all three read as "space these out".
    for (int i = 0; i < 3; ++i) {
        const double o = -6.0 + i * 6.0;
        if (vertical) {
            p.drawRect(QRectF(cx - 6.0, cy + o - 1.0, 12.0, 2.0));
        } else {
            p.drawRect(QRectF(cx + o - 1.0, cy - 6.0, 2.0, 12.0));
        }
    }
    p.restore();
}

}  // namespace

AlignPanel::AlignPanel(QWidget* parent) : QWidget(parent) {
    setAutoFillBackground(true);
    setMouseTracking(true);

    using A = Align;
    const struct {
        A align;
        const char* tip;
    } aligns[] = {
        {A::Left, "Align left edges to the composition's left edge"},
        {A::HCenter, "Centre horizontally in the composition"},
        {A::Right, "Align right edges to the composition's right edge"},
        {A::Top, "Align top edges to the composition's top edge"},
        {A::VCenter, "Centre vertically in the composition"},
        {A::Bottom, "Align bottom edges to the composition's bottom edge"},
    };
    for (int i = 0; i < 6; ++i) {
        buttons_.push_back({QRect(), i, false, aligns[i].align, aligns[i].tip});
    }
    for (int i = 0; i < 6; ++i) {
        buttons_.push_back({QRect(), i, true, A::Left,
                            "Distribute — spaces three or more layers evenly. Ruby "
                            "selects one layer at a time, so this is not available yet."});
    }
    layoutButtons();
}

void AlignPanel::setAlignable(bool alignable) {
    if (alignable_ == alignable) {
        return;
    }
    alignable_ = alignable;
    update();
}

bool AlignPanel::enabled(const Button& b) const {
    // Distribute is never enabled. It needs three layers at once and Ruby has one-layer
    // selection, so it is drawn greyed for the same reason AE greys it: the row is part of
    // the panel's shape, and a panel that changes shape is a panel you have to re-learn.
    return alignable_ && !b.distribute;
}

void AlignPanel::layoutButtons() {
    // Six buttons across, narrowed to whatever room there is rather than keeping their
    // natural width and running off the right edge. This panel lives in a splitter with no
    // minimum, so "there is always 224px" is not a fact about it, it is a hope.
    // No lower bound. A minimum width sounds like it protects the buttons and does the
    // opposite: below it the run stops fitting and goes off the right edge, which is
    // exactly what it was meant to prevent. A 13px button is small; a button you cannot
    // see is not there.
    const int room = width() - kPad * 2 - kBtnGap * 5;
    const int w = std::max(1, std::min(kBtnW, room / 6));

    int y = kPad + kLabelH + kTargetH + kRowGap;
    for (int i = 0; i < 6; ++i) {
        buttons_[static_cast<std::size_t>(i)].rect =
            QRect(kPad + i * (w + kBtnGap), y, w, kBtnH);
    }
    y += kBtnH + kRowGap + kLabelH;
    for (int i = 6; i < 12; ++i) {
        buttons_[static_cast<std::size_t>(i)].rect =
            QRect(kPad + (i - 6) * (w + kBtnGap), y, w, kBtnH);
    }

    // The dropdown starts after the label, measured, not after a number that happened to
    // clear it in the font this was written in.
    QFont small = font();
    small.setPixelSize(type::kColumnHeader);
    labelW_ = QFontMetrics(small).horizontalAdvance(QStringLiteral("Align Layers to:")) + 8;
    const int left = kPad + labelW_;
    targetRect_ = QRect(left, kPad + kLabelH - 2, std::max(52, width() - kPad - left),
                        kTargetH);
}

void AlignPanel::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    layoutButtons();
    update();
}

QRect AlignPanel::buttonRect(int index) const {
    return (index >= 0 && index < static_cast<int>(buttons_.size()))
               ? buttons_[static_cast<std::size_t>(index)].rect
               : QRect();
}

int AlignPanel::buttonCount() const { return static_cast<int>(buttons_.size()); }

int AlignPanel::buttonAt(const QPoint& pos) const {
    for (int i = 0; i < static_cast<int>(buttons_.size()); ++i) {
        if (buttons_[static_cast<std::size_t>(i)].rect.contains(pos)) {
            return i;
        }
    }
    return -1;
}

void AlignPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), kPanelBody);

    QFont small = font();
    small.setPixelSize(type::kColumnHeader);

    p.setFont(small);
    p.setPen(kTextTertiary);
    p.drawText(QRect(kPad, kPad, labelW_, kLabelH), Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("Align Layers to:"));

    // The target dropdown. Composition is the only entry that does anything, so it is the
    // one that shows; Selection is in the menu and disabled, saying why.
    p.setPen(QPen(kDivider, 1.0));
    p.setBrush(kColumnHeader);
    p.drawRect(targetRect_.adjusted(0, 0, -1, -1));
    p.setBrush(Qt::NoBrush);
    p.setFont(font());
    p.setPen(kTextBody);
    p.drawText(targetRect_.adjusted(7, 0, -18, 0), Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("Composition"));
    p.setPen(kTextDim);
    p.drawText(targetRect_.adjusted(0, 0, -7, 0), Qt::AlignVCenter | Qt::AlignRight,
               QStringLiteral("▾"));

    p.setFont(small);
    p.setPen(kTextTertiary);
    p.drawText(QRect(kPad, buttons_[6].rect.top() - kLabelH, 160, kLabelH),
               Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Distribute Layers:"));

    for (int i = 0; i < static_cast<int>(buttons_.size()); ++i) {
        const Button& b = buttons_[static_cast<std::size_t>(i)];
        const bool on = enabled(b);

        if (on && i == hover_) {
            p.fillRect(b.rect, kMenuActive.darker(130));
        }

        const QColor ink = on ? kTextBody : QColor("#4a4a4a");
        if (b.distribute) {
            drawDistributeGlyph(p, b.glyph, b.rect, ink);
        } else {
            drawAlignGlyph(p, b.glyph, b.rect, ink);
        }
    }

    if (!alignable_) {
        p.setFont(small);
        p.setPen(kTextFaint);
        p.drawText(QRect(kPad, buttons_[6].rect.bottom() + kRowGap * 2,
                         width() - kPad * 2, 40),
                   Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap,
                   QStringLiteral("Select a layer with a picture to align it."));
    }
}

void AlignPanel::mousePressEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();

    if (targetRect_.contains(pos)) {
        QMenu menu(this);
        QAction* comp = menu.addAction(QStringLiteral("Composition"));
        comp->setCheckable(true);
        comp->setChecked(true);
        QAction* selection = menu.addAction(
            QStringLiteral("Selection  (needs more than one layer)"));
        selection->setEnabled(false);
        menu.exec(e->globalPosition().toPoint());
        return;
    }

    const int hit = buttonAt(pos);
    if (hit < 0 || !enabled(buttons_[static_cast<std::size_t>(hit)])) {
        return;  // a disabled button swallows its click rather than doing nothing loudly
    }
    emit alignRequested(buttons_[static_cast<std::size_t>(hit)].align);
}

void AlignPanel::mouseMoveEvent(QMouseEvent* e) {
    const int hit = buttonAt(e->position().toPoint());
    if (hit != hover_) {
        hover_ = hit;
        update();
    }
}

void AlignPanel::leaveEvent(QEvent*) {
    if (hover_ != -1) {
        hover_ = -1;
        update();
    }
}

bool AlignPanel::event(QEvent* e) {
    if (e->type() != QEvent::ToolTip) {
        return QWidget::event(e);
    }
    auto* help = static_cast<QHelpEvent*>(e);
    const int hit = buttonAt(help->pos());
    QString text;
    if (hit >= 0) {
        text = QString::fromUtf8(buttons_[static_cast<std::size_t>(hit)].tip);
    } else if (targetRect_.contains(help->pos())) {
        text = QStringLiteral("What layers line up against. Composition means the frame's "
                              "edges and centre.");
    }
    QToolTip::showText(help->globalPos(), text, this);
    e->accept();
    return true;
}

}  // namespace ruby::ui
