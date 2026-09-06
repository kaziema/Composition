#pragma once

#include <QMainWindow>

namespace comp::app {

// Shell only. Panels are placeholders until the engine exists behind them.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void build_menus();
    void build_docks();
    QWidget* make_placeholder(const QString& title, const QString& note);
};

}  // namespace comp::app
