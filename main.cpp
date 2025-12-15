#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication a(argc, argv);

    QFont font("Segoe UI");
    font.setPointSize(10);
    a.setFont(font);

    MainWindow w;
    w.show();

    return a.exec();
}
