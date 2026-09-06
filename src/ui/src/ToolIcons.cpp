#include "comp/ui/ToolIcons.h"

#include <QPainter>
#include <QPainterPath>

namespace comp::ui {
namespace {

constexpr qreal kDesign = 16.0;  // authoring grid

void drawSelection(QPainter& p) {
    QPainterPath path;
    path.moveTo(4.0, 2.0);
    path.lineTo(4.0, 13.4);
    path.lineTo(6.9, 10.6);
    path.lineTo(8.7, 14.2);
    path.lineTo(10.4, 13.4);
    path.lineTo(8.7, 9.9);
    path.lineTo(12.4, 9.6);
    path.closeSubpath();
    p.fillPath(path, p.pen().color());
}

void drawArrowhead(QPainterPath& path, qreal cx, qreal cy, qreal dx, qreal dy) {
    const qreal s = 2.1;
    path.moveTo(cx + dx * s, cy + dy * s);
    path.lineTo(cx - dy * s * 0.62 - dx * 0.3, cy + dx * s * 0.62 - dy * 0.3);
    path.lineTo(cx + dy * s * 0.62 - dx * 0.3, cy - dx * s * 0.62 - dy * 0.3);
    path.closeSubpath();
}

void drawPan(QPainter& p) {
    p.drawLine(QPointF(8.0, 3.6), QPointF(8.0, 12.4));
    p.drawLine(QPointF(3.6, 8.0), QPointF(12.4, 8.0));

    QPainterPath heads;
    drawArrowhead(heads, 8.0, 3.4, 0.0, -1.0);
    drawArrowhead(heads, 8.0, 12.6, 0.0, 1.0);
    drawArrowhead(heads, 3.4, 8.0, -1.0, 0.0);
    drawArrowhead(heads, 12.6, 8.0, 1.0, 0.0);
    p.fillPath(heads, p.pen().color());
}

void drawText(QPainter& p) {
    p.drawLine(QPointF(3.8, 3.6), QPointF(12.2, 3.6));
    p.drawLine(QPointF(8.0, 3.6), QPointF(8.0, 12.8));
}

void drawShape(QPainter& p) {
    p.drawRect(QRectF(3.4, 3.4, 9.2, 9.2));
}

void drawPen(QPainter& p) {
    QPainterPath nib;
    nib.moveTo(8.0, 2.4);
    nib.lineTo(11.8, 9.2);
    nib.lineTo(8.0, 11.6);
    nib.lineTo(4.2, 9.2);
    nib.closeSubpath();
    p.drawPath(nib);
    p.drawLine(QPointF(8.0, 11.6), QPointF(8.0, 14.0));

    QPainterPath hole;
    hole.addEllipse(QPointF(8.0, 8.0), 1.1, 1.1);
    p.fillPath(hole, p.pen().color());
}

void drawMask(QPainter& p) {
    p.drawRect(QRectF(3.4, 3.4, 9.2, 9.2));
    QPainterPath fill;
    fill.moveTo(3.4, 12.6);
    fill.lineTo(12.6, 12.6);
    fill.lineTo(3.4, 3.4);
    fill.closeSubpath();
    p.fillPath(fill, p.pen().color());
}

void drawHand(QPainter& p) {
    QPainterPath palm;
    palm.addRoundedRect(QRectF(4.6, 7.2, 6.8, 6.4), 2.2, 2.2);
    p.drawPath(palm);

    // Three fingers and a thumb, so it reads as a hand at 16px without detail.
    for (int i = 0; i < 3; ++i) {
        const qreal x = 5.1 + static_cast<qreal>(i) * 2.1;
        QPainterPath finger;
        finger.addRoundedRect(QRectF(x, 3.2 + static_cast<qreal>(i == 1 ? -0.7 : 0.0), 1.6,
                                     5.2),
                              0.8, 0.8);
        p.drawPath(finger);
    }
    QPainterPath thumb;
    thumb.addRoundedRect(QRectF(2.8, 8.4, 1.6, 3.6), 0.8, 0.8);
    p.drawPath(thumb);
}

void drawAnchor(QPainter& p) {
    p.drawEllipse(QPointF(8.0, 8.0), 3.1, 3.1);
    p.drawLine(QPointF(8.0, 1.8), QPointF(8.0, 4.2));
    p.drawLine(QPointF(8.0, 11.8), QPointF(8.0, 14.2));
    p.drawLine(QPointF(1.8, 8.0), QPointF(4.2, 8.0));
    p.drawLine(QPointF(11.8, 8.0), QPointF(14.2, 8.0));
}

void drawEffects(QPainter& p) {
    // Stylised "fx": an f with a crossbar, then a small x.
    p.drawLine(QPointF(6.6, 4.6), QPointF(6.6, 12.4));
    QPainterPath hook;
    hook.moveTo(6.6, 5.2);
    hook.cubicTo(6.6, 3.0, 8.6, 2.8, 9.4, 3.6);
    p.drawPath(hook);
    p.drawLine(QPointF(4.6, 7.6), QPointF(8.8, 7.6));

    p.drawLine(QPointF(9.6, 8.8), QPointF(13.0, 12.4));
    p.drawLine(QPointF(13.0, 8.8), QPointF(9.6, 12.4));
}

}  // namespace

void paintToolIcon(QPainter& p, const QRect& box, ToolIcon icon, const QColor& color) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal scale = static_cast<qreal>(qMin(box.width(), box.height())) / kDesign;
    p.translate(box.center().x() + 0.5, box.center().y() + 0.5);
    p.scale(scale, scale);
    p.translate(-kDesign / 2.0, -kDesign / 2.0);

    QPen pen(color);
    pen.setWidthF(1.3);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (icon) {
        case ToolIcon::Selection: drawSelection(p); break;
        case ToolIcon::Pan:       drawPan(p);       break;
        case ToolIcon::Text:      drawText(p);      break;
        case ToolIcon::Shape:     drawShape(p);     break;
        case ToolIcon::Pen:       drawPen(p);       break;
        case ToolIcon::Mask:      drawMask(p);      break;
        case ToolIcon::Hand:      drawHand(p);      break;
        case ToolIcon::Anchor:    drawAnchor(p);    break;
        case ToolIcon::Effects:   drawEffects(p);   break;
    }

    p.restore();
}

}  // namespace comp::ui
