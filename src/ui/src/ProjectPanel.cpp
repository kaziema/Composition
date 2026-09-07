#include "comp/ui/ProjectPanel.h"

#include <QFileInfo>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

#include "comp/ui/Theme.h"

namespace comp::ui {

using namespace theme;

namespace {

constexpr int kSearchH = 26;
constexpr int kTypeW = 52;
constexpr int kDurW = 48;
constexpr int kEdgePad = 8;

QString formatDuration(double seconds) {
    if (seconds <= 0.0) {
        return QStringLiteral("—");
    }
    const int total = static_cast<int>(seconds);
    return QStringLiteral("%1:%2")
        .arg(total / 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

QString formatSize(qint64 bytes) {
    if (bytes <= 0) {
        return QString();
    }
    const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    if (gb >= 1.0) {
        return QStringLiteral("%1 GB").arg(gb, 0, 'f', 1);
    }
    return QStringLiteral("%1 MB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0),
                                       0, 'f', 0);
}

}  // namespace

ProjectPanel::ProjectPanel(QWidget* parent) : QWidget(parent) {
    QFont f = font();
    f.setPixelSize(type::kRowLabel);
    setFont(f);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kEdgePad, 5, kEdgePad, 0);
    layout->setSpacing(0);

    search_ = new QLineEdit(this);
    search_->setPlaceholderText(QStringLiteral("Search"));
    search_->setFixedHeight(kSearchH - 8);
    search_->setStyleSheet(
        QStringLiteral("QLineEdit { background: %1; border: 1px solid %2; color: %3; "
                       "padding: 1px 6px; }")
            .arg(kFieldBg.name(), kFieldBorder.name(), kTextBody.name()));
    layout->addWidget(search_);
    layout->addStretch();

    connect(search_, &QLineEdit::textChanged, this, [this](const QString& text) {
        filter_ = text;
        rebuild();
        update();
    });
}

void ProjectPanel::setProject(const core::Project* project) {
    project_ = project;
    refresh();
}

void ProjectPanel::refresh() {
    rebuild();
    update();
}

void ProjectPanel::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    update();
}

void ProjectPanel::rebuild() {
    rows_.clear();
    totalBytes_ = 0;
    if (project_ == nullptr) {
        return;
    }

    const auto matches = [this](const QString& name) {
        return filter_.isEmpty() || name.contains(filter_, Qt::CaseInsensitive);
    };

    // Compositions above media, the way every editor lists them.
    for (const core::Composition& comp : project_->compositions()) {
        const QString name = QString::fromStdString(comp.name);
        if (!matches(name)) {
            continue;
        }
        rows_.push_back({true, comp.id, name, QStringLiteral("Comp"),
                         formatDuration(comp.duration), kLabelAqua.stripe, 0});
    }

    for (const core::MediaItem& item : project_->media()) {
        const QString name = QString::fromStdString(item.name);
        if (!matches(name)) {
            continue;
        }

        // Resolution is more use than a codec name when you are picking a clip.
        QString kind = QStringLiteral("—");
        QColor swatch = kLabelGray.stripe;
        if (item.isVideo()) {
            kind = (item.height >= 2160) ? QStringLiteral("4K")
                 : (item.height >= 1080) ? QStringLiteral("HD")
                                         : QStringLiteral("SD");
        } else {
            kind = QStringLiteral("Aud");
            swatch = kLabelGreen.stripe;
        }

        const qint64 bytes =
            QFileInfo(QString::fromStdString(item.path)).size();
        totalBytes_ += bytes;

        rows_.push_back({false, item.id, name, kind, formatDuration(item.duration),
                         swatch, bytes});
    }

    if (selected_ >= static_cast<int>(rows_.size())) {
        selected_ = -1;
    }
}

int ProjectPanel::rowAt(int y) const {
    const int top = kSearchH + metrics::kColumnHeaderH;
    if (y < top) {
        return -1;
    }
    const int index = (y - top) / metrics::kProjectRowH;
    return (index >= 0 && index < static_cast<int>(rows_.size())) ? index : -1;
}

void ProjectPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), kPanelBody);

    const int headerY = kSearchH;
    const int listY = headerY + metrics::kColumnHeaderH;
    const int nameW = width() - kTypeW - kDurW - kEdgePad;

    // Column header.
    p.fillRect(QRect(0, headerY, width(), metrics::kColumnHeaderH), kColumnHeader);
    QFont small = font();
    small.setPixelSize(type::kColumnHeader);
    p.setFont(small);
    p.setPen(kColumnHeaderText);
    p.drawText(QRect(kEdgePad + 20, headerY, nameW, metrics::kColumnHeaderH),
               Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Name"));
    p.drawText(QRect(nameW, headerY, kTypeW, metrics::kColumnHeaderH),
               Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Type"));
    p.drawText(QRect(nameW + kTypeW, headerY, kDurW, metrics::kColumnHeaderH),
               Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Dur"));

    const int footerY = height() - metrics::kProjectFooterH;

    if (rows_.empty()) {
        p.setFont(font());
        p.setPen(kTextFaint);
        p.drawText(QRect(0, listY, width(), footerY - listY), Qt::AlignCenter,
                   project_ == nullptr || project_->media().empty()
                       ? QStringLiteral("File > Import Media\nto get started")
                       : QStringLiteral("nothing matches"));
    }

    p.setFont(font());
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        const int y = listY + i * metrics::kProjectRowH;
        if (y + metrics::kProjectRowH > footerY) {
            break;  // no scrolling yet; the pool is small
        }
        const Row& row = rows_[static_cast<std::size_t>(i)];

        const QColor background = (i == selected_) ? kRowSelected
                                 : (i % 2 == 0)    ? kSubToolbar
                                                   : kPanelBody;
        p.fillRect(QRect(0, y, width(), metrics::kProjectRowH), background);

        // Twirl slot, then the swatch. Compositions can hold layers; media cannot.
        if (row.isComposition) {
            p.setPen(kTextDim);
            p.drawText(QRect(kEdgePad - 2, y, 10, metrics::kProjectRowH), Qt::AlignCenter,
                       QStringLiteral("▸"));
        }
        p.fillRect(QRect(kEdgePad + 9, y + (metrics::kProjectRowH - 10) / 2, 13, 10),
                   row.swatch);
        p.setPen(kFieldBorder);
        p.drawRect(QRect(kEdgePad + 9, y + (metrics::kProjectRowH - 10) / 2, 13, 10));

        p.setPen(i == selected_ ? kTextSelectedLayer : kTextBody);
        p.drawText(QRect(kEdgePad + 28, y, nameW - kEdgePad - 28, metrics::kProjectRowH),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetrics(font()).elidedText(row.name, Qt::ElideMiddle,
                                                   nameW - kEdgePad - 30));

        QFont mono = font();
        mono.setFamily(monoFontFamily());
        mono.setPixelSize(type::kMeta);
        p.setFont(mono);
        p.setPen(kTextDim);
        p.drawText(QRect(nameW, y, kTypeW, metrics::kProjectRowH),
                   Qt::AlignVCenter | Qt::AlignLeft, row.type);
        p.drawText(QRect(nameW + kTypeW, y, kDurW, metrics::kProjectRowH),
                   Qt::AlignVCenter | Qt::AlignLeft, row.duration);
        p.setFont(font());
    }

    // Footer: how much is in here, and how heavy it is.
    p.fillRect(QRect(0, footerY, width(), metrics::kProjectFooterH), kColumnHeader);
    QFont mono = font();
    mono.setFamily(monoFontFamily());
    mono.setPixelSize(type::kMeta);
    p.setFont(mono);
    p.setPen(kTextDim);
    p.drawText(QRect(kEdgePad, footerY, width() / 2, metrics::kProjectFooterH),
               Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("%1 item%2")
                   .arg(rows_.size())
                   .arg(rows_.size() == 1 ? QString() : QStringLiteral("s")));
    p.drawText(QRect(width() / 2, footerY, width() / 2 - kEdgePad,
                     metrics::kProjectFooterH),
               Qt::AlignVCenter | Qt::AlignRight, formatSize(totalBytes_));
}

void ProjectPanel::mousePressEvent(QMouseEvent* e) {
    const int row = rowAt(e->position().toPoint().y());
    if (row != selected_) {
        selected_ = row;
        update();
    }
}

void ProjectPanel::mouseDoubleClickEvent(QMouseEvent* e) {
    const int index = rowAt(e->position().toPoint().y());
    if (index < 0) {
        return;
    }
    const Row& row = rows_[static_cast<std::size_t>(index)];
    if (row.isComposition) {
        emit compositionActivated(row.id);
    } else {
        emit mediaActivated(row.id);
    }
}

}  // namespace comp::ui
