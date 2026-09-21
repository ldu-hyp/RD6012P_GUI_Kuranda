#include "MainWindow.h"

#include <QApplication>
#include <QFont>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("RD6012P GUI Kuranda"));
    app.setOrganizationName(QStringLiteral("Kuranda"));
    app.setApplicationVersion(QStringLiteral("0.2.0"));

    QFont font(QStringLiteral("Segoe UI"));
    font.setPointSize(10);
    app.setFont(font);

    MainWindow window;
    window.show();

    return app.exec();
}
