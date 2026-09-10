#pragma once

#include <QStringList>
#include <QWidget>

class QStackedWidget;

namespace ruby::ui {

// A panel group: a 26px tab strip over a content area. Each tab carries a 4px dot
// (accent when active, #555 when not).
// Tabs are how panels group in this design, so this is the base unit of the whole
// layout rather than a one-off.
//
// Tabs can be dragged between frames. A frame owns whichever tabs it currently holds and
// nothing above it decides that: drop the Inspector into the left dock and it lives in the
// left dock, including when the left dock is showing the effects group.
//
// What this deliberately does NOT have is free-form docking, the five-zone overlay AE and
// Premiere throw up when you drag a panel. Dropping onto a tab strip inserts at a line you
// can see. Same capability, one target instead of five, nothing to learn.
class PanelFrame : public QWidget {
    Q_OBJECT

public:
    explicit PanelFrame(const QStringList& tabs, QWidget* parent = nullptr);
    ~PanelFrame() override;

    // Content is parallel to the tab list: index N belongs to tab N.
    void addPage(QWidget* page);

    // Replaces the tab labels without touching the pages. Used where the tabs describe
    // what one page is showing (the timeline's open compositions) rather than selecting
    // between different pages.
    void setTabs(const QStringList& tabs);

    // Whether this frame's tabs can be dragged out.
    //
    // False for a frame whose tabs are not pages. The timeline lists open compositions
    // over a single shared page, so its tabs name a thing inside the panel rather than
    // naming panels, and dragging one into the Inspector would mean nothing.
    void setTabsMovable(bool movable);
    [[nodiscard]] bool tabsMovable() const noexcept { return movable_; }

    [[nodiscard]] int tabCount() const;
    [[nodiscard]] QString tabLabel(int index) const;
    void setTabLabel(int index, const QString& label);

    // Which frame currently holds `page`, and at what index. Null when nobody does.
    //
    // Necessary because a tab's home is no longer fixed: code that wants to rename the
    // viewer's composition tab cannot assume the viewer still has it.
    [[nodiscard]] static PanelFrame* frameHolding(QWidget* page, int* indexOut = nullptr);
    [[nodiscard]] int currentIndex() const;
    void setCurrentIndex(int index);

    // Removes a tab and its page and hands both back. The page is reparented to nobody,
    // so the caller owns it until it is inserted somewhere.
    struct DetachedTab {
        QString label;
        QWidget* page = nullptr;
    };
    [[nodiscard]] DetachedTab takeTab(int index);
    void insertTab(int index, const QString& label, QWidget* page);

signals:
    void currentChanged(int index);

    // A tab arrived or left. The window listens so it can hide a frame that has emptied
    // and notice when a panel it had a button for has moved.
    void tabsChanged();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    class TabStrip;

    TabStrip* strip_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    bool movable_ = true;
};

}  // namespace ruby::ui
