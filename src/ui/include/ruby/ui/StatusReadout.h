#pragma once

#include <QColor>
#include <QString>
#include <QWidget>
#include <vector>

namespace ruby::ui {

// The right end of the status bar: machine and engine readouts.
//
// A list rather than a fixed set of labels, because this is where CPU load, memory,
// cache coverage, decode backlog and render queue state all end up, and each of those
// arriving should be one line at the call site rather than a new widget.
class StatusReadout : public QWidget {
    Q_OBJECT

public:
    struct Item {
        QString text;
        QColor dot;       // invalid means no dot
        bool warn = false;  // draws in the warning colour rather than the dim one
    };

    explicit StatusReadout(QWidget* parent = nullptr);

    void setItems(std::vector<Item> items);

protected:
    void paintEvent(QPaintEvent*) override;
    [[nodiscard]] QSize sizeHint() const override;

private:
    std::vector<Item> items_;
};

}  // namespace ruby::ui
