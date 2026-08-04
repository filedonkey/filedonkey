#include "mainwindow.h"
#include "virtdisk.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QLoggingCategory>

int main(int argc, char *argv[])
{
    // Mount helper, started by VirtDisk::mount() on macOS - see the comment there for why the
    // mount cannot share a process with any other. It serves one peer and nothing else: no
    // window, no discovery, no server, so QCoreApplication is all it needs.
    if (argc == 6 && QString::fromUtf8(argv[1]) == "--mount")
    {
        QCoreApplication worker(argc, argv);

        Connection conn = {
            .machineId      = QString::fromUtf8(argv[2]),
            .machineName    = QString::fromUtf8(argv[3]),
            .machineAddress = QString::fromUtf8(argv[4]),
            .machinePort    = QString::fromUtf8(argv[5]).toLongLong(),
        };

        VirtDisk virtDisk(conn);
        return virtDisk.runMountWorker();
    }

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
