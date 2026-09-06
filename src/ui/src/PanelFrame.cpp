#include "comp/ui/PanelFrame.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "comp/ui/Theme.h"

namespace comp::ui {

using namespace theme;

// --- Tab strip ---------------------------------------------------------------

class PanelFrame::TabStrip : public QWidget {
public:
    explicit TabStrip(const QStringList& labels, QWidget* parent)
        : QWidget(parent), labels_(labels) {
        setFixedHeight(metrics::kTabStripH);
        setMouseTracking(true);
        QFont f = font();
        f.setPixelSize(type::kTabLabel);
        setFont(f);
        layoutTabs();
    }

    [[nodiscard]] int current() const { return current_; }

    void setCurrent(int index) {
        if (index < 0 || index >= labels_.size() || index == current_) {
            return;
        }
        current_ = index;
        update();
    }

    std::function<void(int)> onActivate;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), kTabStrip);

        for (int i = 0; i < labels_.size(); ++i) {
            const QRect r = tabRects_.at(i);
            const bool active = (i == current_);

            if (active) {
                p.fillRect(r, kTabActiveBg);
            } else if (i == hover_) {
                p.fillRect(r, kMenuActive.darker(130));
            }

            // 4px dot, then the label.
            const QColor dot = active ? kTabDotActive : kTabDotInactive;
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(dot);
            p.drawEllipse(QPointF(r.left() + kPadX + 2.0, r.center().y() + 0.5), 2.0, 2.0);
            p.setRenderHint(QPainter::Antialiasing, false);

            p.setPen(active ? kTabActiveText : kTabInactiveText);
            const QRect textRect = r.adjusted(kPadX + kDotW, 0, -kPadX, 0);
            p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, labels_.at(i));
        }

        // Hard rule under the strip.
        p.setPen(kDivider);
        p.drawLine(0, height() - 1, width(), height() - 1);
    }

    void mousePressEvent(QMouseEvent* e) override {
        const int hit = tabAt(e->position().toPoint());
        if (hit >= 0 && hit != current_) {
            setCurrent(hit);
            if (onActivate) {
                onActivate(hit);
            }
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        const int hit = tabAt(e->position().toPoint());
        if (hit != hover_) {
            hover_ = hit;
            update();
        }
    }

    void leaveEvent(QEvent*) override {
        if (hover_ != -1) {
            hover_ = -1;
            update();
        }
    }

private:
    static constexpr int kPadX = 9;
    static constexpr int kDotW = 10;

    void layoutTabs() {
        const QFontMetrics fm(font());
        tabRects_.clear();
        int x = 0;
        for (const QString& label : labels_) {
            const int w = kPadX + kDotW + fm.horizontalAdvance(label) + kPadX;
            tabRects_.append(QRect(x, 0, w, metrics::kTabStripH));
            x += w;
        }
    }

    [[nodiscard]] int tabAt(const QPoint& pos) const {
        for (int i = 0; i < tabRects_.size(); ++i) {
            if (tabRects_.at(i).contains(pos)) {
                return i;
            }
        }
        return -1;
    }

    QStringList labels_;
    QList<QRect> tabRects_;
    int current_ = 0;
    int hover_ = -1;
};

// --- PanelFrame --------------------------------------------------------------

PanelFrame::PanelFrame(const QStringList& tabs, QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    strip_ = new TabStrip(tabs, this);
    stack_ = new QStackedWidget(this);

    layout->addWidget(strip_);
    layout->addWidget(stack_, 1);

    strip_->onActivate = [this](int index) {
        stack_->setCurrentIndex(index);
        emit currentChanged(index);
    };

    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, kPanelBody);
    setPalette(pal);
}

void PanelFrame::addPage(QWidget* page) { stack_->addWidget(page); }

int PanelFrame::currentIndex() const { return strip_->current(); }

void PanelFrame::setCurrentIndex(int index) {
    strip_->setCurrent(index);
    stack_->setCurrentIndex(index);
}

}  // namespace comp::ui
