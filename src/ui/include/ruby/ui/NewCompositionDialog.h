#pragma once

#include <QDialog>
#include <QString>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;

namespace ruby::ui {

// Settings for a new composition.
//
// Presets first, fields second. Almost nobody types 1080 by 1920 on purpose; they pick
// "Vertical" because that is the shape of the thing they are making. The fields exist
// for the case the presets do not cover, not as the primary way in.
class NewCompositionDialog : public QDialog {
    Q_OBJECT

public:
    struct Settings {
        QString name;
        int width = 1080;
        int height = 1920;
        double fps = 30.0;
        double duration = 15.0;
    };

    explicit NewCompositionDialog(QWidget* parent = nullptr);

    // Same dialog, seeded with an existing composition: the fields are identical, so a
    // second one would be the same code with a different title.
    //
    // This is the only way to shorten a composition. Duration grows on its own whenever
    // a layer runs past the end, and never shrinks on its own, so this dialog is the
    // release valve for that. contentEnd is shown but not enforced: setting a duration
    // below it leaves layers hanging over the end rather than trimming them.
    NewCompositionDialog(const Settings& existing, double contentEnd,
                         QWidget* parent = nullptr);

    [[nodiscard]] Settings settings() const;

private:
    void build(bool editing, double contentEnd);
    void applyPreset(int index);

    QLineEdit* name_ = nullptr;
    QComboBox* preset_ = nullptr;
    QSpinBox* width_ = nullptr;
    QSpinBox* height_ = nullptr;
    QComboBox* fps_ = nullptr;
    QDoubleSpinBox* duration_ = nullptr;
};

}  // namespace ruby::ui
