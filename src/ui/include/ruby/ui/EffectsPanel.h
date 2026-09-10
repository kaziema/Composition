#pragma once

#include <QString>
#include <QWidget>
#include <vector>

#include "ruby/core/Document.h"

class QLineEdit;

namespace ruby::ui {

// Effects and presets, searchable, with drag onto a layer.
//
// The Effect menu can apply an effect and nobody browses a menu bar to work. In AE the
// menu exists and the panel is what people actually use: type three letters, drag the
// result onto a layer. That is what this is.
//
// It is also where presets live when they exist, which is why the tabs are here from the
// start rather than being added later. AE calls it "Effects & Presets" for a reason, and
// presets are the product: the half that does not exist yet should have a home waiting
// rather than needing one built for it.
class EffectsPanel : public QWidget {
    Q_OBJECT

public:
    explicit EffectsPanel(QWidget* parent = nullptr);

    // Which tab is showing. Driven by the panel frame's tabs rather than owned here.
    enum class Tab { Effects, Presets, ColorCorrection };
    void setTab(Tab tab);

    // MIME type carrying an effect id, so the timeline accepts a drop from here and
    // rejects one from anywhere else. Needs its own access specifier for the same reason
    // ProjectPanel's does: everything after `signals:` is a signal until one appears.
    static const char* effectMimeType();

signals:
    // Double-clicked, or dragged onto nothing in particular. The window applies it to the
    // selected layer, because applying is an undoable document edit.
    void effectActivated(const std::string& effectId);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    // A row is either a category heading or an effect under it. One flat list rather than
    // a tree, because a two-level tree that is always fully expanded is a list with extra
    // machinery.
    struct Row {
        bool isCategory = false;
        QString label;
        std::string effectId;
    };

    void rebuild();
    [[nodiscard]] int rowAt(int y) const;

    Tab tab_ = Tab::Effects;
    std::vector<Row> rows_;
    QLineEdit* search_ = nullptr;
    QString filter_;
    int selected_ = -1;
    QPoint pressAt_;
    bool maybeDragging_ = false;
};

}  // namespace ruby::ui
