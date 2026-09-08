#pragma once

#include <QPoint>
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

public:
    // MIME type carrying a MediaId, so the timeline can accept a drop from here and
    // reject a drop from anywhere else. Needs its own access specifier: everything after
    // `signals:` is a signal until one appears, and moc will try to generate this.
    static const char* mediaMimeType();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    struct Row {
        bool isComposition = false;
        std::uint64_t id = 0;
        QString name;
        QString type;
        QString duration;
        QColor swatch;
        qint64 bytes = 0;
    };

    [[nodiscard]] int rowAt(int y) const;
    void rebuild();

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
