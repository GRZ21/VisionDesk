#include <QApplication>
#include <QPushButton>
#include <QMessageBox>

#include "database/DatabaseManager.h"
#include "widget/MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    QApplication::setApplicationName("VisionDesk");
    QApplication::setApplicationDisplayName("VisionDesk");

    if (DatabaseManager::instance().initDatabase().isEmpty())
        return 0;
    if (!DatabaseManager::instance().openDatabase()) {
        QMessageBox::critical(nullptr, "数据库初始化失败", DatabaseManager::instance().lastError());
        return -1;
    }

    MainWindow w;
    w.show();
    return QApplication::exec();
}
