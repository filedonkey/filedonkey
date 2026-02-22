#include "mainwindow.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QLoggingCategory>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "FileDonkey_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }

    // QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, false);

    // QApplication::setQuitOnLastWindowClosed(false);

    MainWindow w;
    w.show();
    return a.exec();
}
