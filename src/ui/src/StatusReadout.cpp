#include "ruby/ui/StatusReadout.h"

#include <QFontMetrics>
#include <QPainter>

#include "ruby/ui/Theme.h"

namespace ruby::ui {

using namespace theme;

namespace {
constexpr int kGap = 16;      // between readouts
constexpr int kDotGap = 7;    // between a dot and its text
constexpr int kDotRadius = 3;
constexpr int kEdgePad = 9;
}  // namespace

StatusReadout::StatusReadout(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    QFont f;
    f.setFamily(monoFontFamily());
    f.setPixelSize(type::kMeta);
    setFont(f);
}

void StatusReadout::setItems(std::vector<Item> items) {
    items_ = std::move(items);
    // updateGeometry() alone is not enough inside a QStatusBar: it sizes permanent
    // widgets once and does not re-run its layout for a changed hint, so the widget
    // keeps whatever tiny width it had when it was empty and paints into nothing.
    const QSize wanted = sizeHint();
    setMinimumSize(wanted);
    resize(wanted);
    updateGeometry();
    update();
}

QSize StatusReadout::sizeHint() const {
    const QFontMetrics fm(font());
    int width = kEdgePad;
    for (const Item& item : items_) {
        if (item.dot.isValid()) {
            width += kDotRadius * 2 + kDotGap;
        }
        width += fm.horizontalAdvance(item.text) + kGap;
    }
    return {width, fm.height() + 4};
}

void StatusReadout::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QFontMetrics fm(font());

    // Laid out right to left, so the rightmost readout stays put as others come and go.
    int x = width() - kEdgePad;
    for (auto it = items_.rbegin(); it != items_.rend(); ++it) {
        const int textWidth = fm.horizontalAdvance(it->text);
        x -= textWidth;

        p.setPen(it->warn ? kValueScrubbable : kTextDim);
        p.drawText(QRect(x, 0, textWidth, height()), Qt::AlignVCenter | Qt::AlignLeft,
                   it->text);

        if (it->dot.isValid()) {
            x -= kDotGap;
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(it->dot);
            p.drawEllipse(QPointF(x - kDotRadius, height() / 2.0), kDotRadius,
                          kDotRadius);
            p.setRenderHint(QPainter::Antialiasing, false);
            x -= kDotRadius * 2;
        }
        x -= kGap;
    }
}

}  // namespace ruby::ui
