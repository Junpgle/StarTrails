#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    // Enable high DPI support
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication a(argc, argv);

    // Set global font
    QFont font("Segoe UI");
    font.setPointSize(10);
    a.setFont(font);

    // 修复：类名必须是 'MainWindow' (首字母大写)，不能是 'mainwindow'
    MainWindow w;
    w.show();

    return a.exec();
}
