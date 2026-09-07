#include "ruby/ui/NewCompositionDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "ruby/ui/Theme.h"

namespace ruby::ui {
namespace {

struct Preset {
    const char* label;
    int width;
    int height;
};

// Vertical first, because that is what this app is for. A short-form editor that opens
// on 1920x1080 is asking every single user to change it every single time.
constexpr Preset kPresets[] = {
    {"Vertical  ·  1080 x 1920  ·  TikTok, Reels, Shorts", 1080, 1920},
    {"Square  ·  1080 x 1080", 1080, 1080},
    {"Landscape  ·  1920 x 1080", 1920, 1080},
    {"Landscape 4K  ·  3840 x 2160", 3840, 2160},
    {"Custom", 0, 0},
};

}  // namespace

NewCompositionDialog::NewCompositionDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("New Composition"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    form->setSpacing(8);

    name_ = new QLineEdit(QStringLiteral("Comp 1"), this);
    form->addRow(QStringLiteral("Name"), name_);

    preset_ = new QComboBox(this);
    for (const Preset& p : kPresets) {
        preset_->addItem(QString::fromUtf8(p.label));
    }
    form->addRow(QStringLiteral("Preset"), preset_);

    width_ = new QSpinBox(this);
    width_->setRange(16, 16384);
    width_->setSingleStep(2);
    form->addRow(QStringLiteral("Width"), width_);

    height_ = new QSpinBox(this);
    height_->setRange(16, 16384);
    height_->setSingleStep(2);
    form->addRow(QStringLiteral("Height"), height_);

    fps_ = new QComboBox(this);
    // 23.976 and 29.97 are here because real footage arrives at them, and a comp that
    // cannot match its source frame rate makes every cut land between frames.
    for (const char* rate : {"23.976", "24", "25", "29.97", "30", "50", "60"}) {
        fps_->addItem(QString::fromUtf8(rate));
    }
    fps_->setCurrentText(QStringLiteral("30"));
    form->addRow(QStringLiteral("Frame rate"), fps_);

    duration_ = new QDoubleSpinBox(this);
    duration_->setRange(0.5, 3600.0);
    duration_->setDecimals(2);
    duration_->setSuffix(QStringLiteral(" s"));
    duration_->setValue(15.0);
    form->addRow(QStringLiteral("Duration"), duration_);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Create"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(preset_, &QComboBox::currentIndexChanged, this,
            &NewCompositionDialog::applyPreset);

    // Typing a size that stops matching the preset should say so rather than leaving a
    // label that quietly lies about what you are making.
    const auto markCustom = [this] {
        for (int i = 0; i < static_cast<int>(std::size(kPresets)); ++i) {
            if (kPresets[i].width == width_->value() &&
                kPresets[i].height == height_->value()) {
                preset_->setCurrentIndex(i);
                return;
            }
        }
        preset_->setCurrentIndex(static_cast<int>(std::size(kPresets)) - 1);
    };
    connect(width_, &QSpinBox::valueChanged, this, markCustom);
    connect(height_, &QSpinBox::valueChanged, this, markCustom);

    applyPreset(0);
    name_->setFocus();
    name_->selectAll();
}

void NewCompositionDialog::applyPreset(int index) {
    if (index < 0 || index >= static_cast<int>(std::size(kPresets))) {
        return;
    }
    const Preset& preset = kPresets[index];
    if (preset.width == 0) {
        return;  // Custom leaves whatever is already there
    }
    QSignalBlocker blockWidth(width_);
    QSignalBlocker blockHeight(height_);
    width_->setValue(preset.width);
    height_->setValue(preset.height);
}

NewCompositionDialog::Settings NewCompositionDialog::settings() const {
    Settings out;
    out.name = name_->text().trimmed();
    if (out.name.isEmpty()) {
        out.name = QStringLiteral("Comp");
    }
    out.width = width_->value();
    out.height = height_->value();
    out.fps = fps_->currentText().toDouble();
    out.duration = duration_->value();
    return out;
}

}  // namespace ruby::ui
