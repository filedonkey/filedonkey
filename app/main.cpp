#include "mainwindow.h"
#include "virtdisk.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QLoggingCategory>

#if !defined(_WIN32)
#include <QSocketNotifier>

#include <csignal>
#include <unistd.h>

// A signal kills the event loop where it stands: exec() never returns, so ~MainWindow never runs
// and nothing stops the mounts. They are left behind with the app gone - on macOS the mount helper
// processes too, reparented to init with their volumes still in the mount table. Turn the signal
// into an ordinary quit instead, so teardown happens the same way it does for Quit in the menu.
//
// Writing to a pipe is about all a handler may safely do; the notifier picks it up back on the
// event loop, where calling into Qt is allowed again.
static int quitPipe[2];

static void onQuitSignal(int)
{
    const char byte = 1;
    ssize_t written = write(quitPipe[1], &byte, 1);
    (void)written;
}

static void installQuitSignalHandlers(QCoreApplication *app)
{
    if (pipe(quitPipe) != 0) return;

    QSocketNotifier *notifier = new QSocketNotifier(quitPipe[0], QSocketNotifier::Read, app);

    // Disabled first, or it keeps firing on the still-unread byte until exec() gets around to
    // returning.
    QObject::connect(notifier, &QSocketNotifier::activated, app, [notifier]() {
        notifier->setEnabled(false);
        QCoreApplication::quit();
    });

    struct sigaction action = {};
    action.sa_handler = onQuitSignal;
    sigemptyset(&action.sa_mask);

    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT,  &action, nullptr);
    sigaction(SIGHUP,  &action, nullptr);
}
#endif

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

#if !defined(_WIN32)
    // Only the GUI process: the mount helper above already has libfuse's own handlers, installed
    // by fuse_set_signal_handlers(), and they are what a terminate() from here relies on.
    installQuitSignalHandlers(&a);
#endif

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
