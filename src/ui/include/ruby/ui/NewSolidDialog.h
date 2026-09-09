#pragma once

#include <QColor>
#include <QDialog>
#include <QString>

class QLineEdit;
class QPushButton;
class QSpinBox;

namespace ruby::ui {

// Solid settings, the way AE asks for them: name, size, colour, and a button to snap the
// size back to the composition.
//
// A solid you cannot colour is not worth making, and there is no colour control in the
// inspector yet, so the dialog is currently the only way to set one. That is also AE's
// flow, which is a decent sign it is not the wrong shape.
class NewSolidDialog : public QDialog {
    Q_OBJECT

public:
    struct Settings {
        QString name;
        QColor color;
        int width = 0;   // 0 means "match the composition"
        int height = 0;
    };

    // compWidth/compHeight seed the size fields and back the "Make Comp Size" button.
    NewSolidDialog(int compWidth, int compHeight, QWidget* parent = nullptr);

    [[nodiscard]] Settings settings() const;

private:
    void pickColor();
    void updateSwatch();

    int compWidth_ = 1080;
    int compHeight_ = 1920;
    QColor color_;

    QLineEdit* name_ = nullptr;
    QPushButton* swatch_ = nullptr;
    QSpinBox* width_ = nullptr;
    QSpinBox* height_ = nullptr;
};

}  // namespace ruby::ui
