#pragma once

#include <QStringList>
#include <QWidget>

class QStackedWidget;

namespace comp::ui {

// A panel group: a 26px tab strip over a content area, matching the handoff's
// classic chrome. Each tab carries a 4px dot (accent when active, #555 when not).
// Tabs are how panels group in this design, so this is the base unit of the whole
// layout rather than a one-off.
class PanelFrame : public QWidget {
    Q_OBJECT

public:
    explicit PanelFrame(const QStringList& tabs, QWidget* parent = nullptr);

    // Content is parallel to the tab list: index N belongs to tab N.
    void addPage(QWidget* page);

    [[nodiscard]] int currentIndex() const;
    void setCurrentIndex(int index);

signals:
    void currentChanged(int index);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    class TabStrip;

    TabStrip* strip_ = nullptr;
    QStackedWidget* stack_ = nullptr;
};

}  // namespace comp::ui
