#pragma once

#include <QPoint>
#include <QRect>
#include <QString>
#include <QWidget>
#include <cstdint>
#include <string>
#include <vector>

#include "ruby/core/Document.h"

class QLineEdit;

namespace ruby::ui {

// The project panel: compositions and imported media, per the design's three-column
// list. Custom-painted for the same reason as the timeline and inspector — fixed 22px
// rows, alternating backgrounds, a colour swatch per row, and a footer strip.
class ProjectPanel : public QWidget {
    Q_OBJECT

public:
    explicit ProjectPanel(QWidget* parent = nullptr);

    void setProject(const core::Project* project);

    // Call after anything changes the pool or the comp list.
    void refresh();

signals:
    // Double-clicked a media item: put it in the current composition.
    void mediaActivated(core::MediaId media);
    void compositionActivated(core::CompId comp);

    // Footer buttons. The window owns all three, because all three are undoable document
    // edits and the panel does not do those.
    void newCompositionRequested();

    // Footage dropped on the New Composition button: make a composition that matches it.
    void compositionFromMediaRequested(core::MediaId media);
    void deleteRequested(bool isComposition, std::uint64_t id);

public:
    // MIME type carrying a MediaId, so the timeline can accept a drop from here and
    // reject a drop from anywhere else. Needs its own access specifier: everything after
    // `signals:` is a signal until one appears, and moc will try to generate this.
    static const char* mediaMimeType();

protected:
    void paintEvent(QPaintEvent*) override;
    bool event(QEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    struct Row {
        bool isComposition = false;
        std::uint64_t id = 0;
        QString name;
        QString type;
        QString duration;
        QColor swatch;
        qint64 bytes = 0;

        // Everything the columns do not have room for: resolution, frame rate, path.
        // Shown on hover, because the panel is narrow and most of this is only wanted
        // occasionally.
        QString detail;
    };

    [[nodiscard]] int rowAt(int y) const;
    void rebuild();
    void layoutFooter();

    // Left to right along the footer. Deliberately few: AE has six and two of them have
    // nothing to say in Ruby. See NOTEBOOK 6.11.
    QRect newCompRect_;
    QRect deleteRect_;
    QRect countRect_;
    QRect sizeRect_;
    int hoverButton_ = -1;
    bool dropOnNewComp_ = false;

    const core::Project* project_ = nullptr;
    std::vector<Row> rows_;
    QLineEdit* search_ = nullptr;
    QString filter_;
    int selected_ = -1;
    QPoint pressAt_;
    bool maybeDragging_ = false;
    qint64 totalBytes_ = 0;
};

}  // namespace ruby::ui
