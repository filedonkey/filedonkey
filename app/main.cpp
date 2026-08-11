#include "mainwindow.h"
#include "singleinstance.h"
#include "virtdisk.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
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

// The two fonts the interface is set in, neither of which can be left to the platform to pick.
//
// Inconsolata - what the device rows are in - is installed on no desktop by default, not even the
// one this was designed on, so the stylesheet has been naming a font nobody had. It travels in the
// resources now and is registered here, before any widget exists to be styled with it.
//
// The rest of the interface follows the desktop's own UI font, which is already what Qt puts in the
// application font: Segoe UI on Windows, San Francisco on macOS, whatever the user has set on
// Linux. The stylesheet used to name Segoe outright - see the QWidget rule in filedonkey.qss for
// what that did to the other two.
//
// Windows 11 is the one place worth correcting Qt on. The message font it reports is still plain
// Segoe UI, while the shell sets body text in Segoe UI Variable Text, the same typeface cut for
// this size. Take that where it exists and keep what Qt gave us where it does not.
//
// Size is not set here. The QWidget rule gives everything 13px and would override whatever point
// size came back from the platform anyway.
static void applyFonts()
{
    if (QFontDatabase::addApplicationFont(":/assets/Inconsolata-SemiBold.ttf") == -1)
    {
        qWarning() << "[applyFonts] could not register Inconsolata; the device rows fall back to the platform's mono";
    }

    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);

#if defined(_WIN32)
    if (QFontDatabase::families().contains("Segoe UI Variable Text"))
    {
        font.setFamily("Segoe UI Variable Text");
    }
#endif

    QApplication::setFont(font);
}

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

    // QSettings reads these; without them it files everything under an "Unknown Organization"
    // that moves the moment the binary is renamed.
    QCoreApplication::setOrganizationName("FileDonkey");
    QCoreApplication::setApplicationName("FileDonkey");

    // After the --mount branch above, deliberately: the mount helper is meant to run many at once,
    // one per peer, and is not the instance this guards.
    SingleInstance instance;
    if (!instance.claim())
    {
        return 0;
    }

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

    // Before MainWindow, so the application font is in place by the time its widgets are built and
    // none of them has to be re-polished for it.
    applyFonts();

    MainWindow w;

    // A second start hands its job to us rather than running: bring the window back for it.
    QObject::connect(&instance, &SingleInstance::showRequested, &w, &MainWindow::restoreWindow);

    w.show();
    return a.exec();
}
