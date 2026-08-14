#include "autostart.h"
#include "mainwindow.h"
#include "singleinstance.h"
#include "virtdisk.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QLocale>
#include <QSystemTrayIcon>
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

// The one font the interface is set in: Inconsolata, on every widget rather than on the device
// rows alone, so all three platforms show the same window. It is installed on no desktop by
// default, so it travels in the resources and is registered here, before any widget exists to be
// styled with it.
//
// Two cuts. The regular is what the window is mostly in; the semibold is what the rules naming
// font-weight: 600 resolve to. Only the semibold used to travel, which is why every row was in it
// - it was the one face in the family and the rules asking for no weight got it by default.
//
// Nothing follows the desktop's UI font any more. That was the old arrangement - Segoe UI on
// Windows, San Francisco on macOS - with the stylesheet naming a mono family on the rows to
// override it, and Windows 11 corrected to Segoe UI Variable Text on top.
//
// Registering the files is the half that has to happen here; the family is named by the QWidget
// rule in filedonkey.qss, and that rule is what actually dresses the window. See the comment on it
// for why the application font set below is not enough on its own - macOS puts the system font in
// front of it for most widget classes, which is what left the mac build in San Francisco while
// Windows took the font it was given. The font is still set, for the paths that read
// QApplication::font() before a widget has been polished.
//
// What a platform registers a face under is not ours to decide, so ask rather than assume: the
// warning names the families that arrived, which is the thing worth knowing when a build comes up
// in the wrong type.
//
// Size is not set here. The QWidget rule gives everything 13px and would override whatever point
// size came back from the platform anyway.
static void applyFonts()
{
    const int regular  = QFontDatabase::addApplicationFont(":/assets/Inconsolata-Regular.ttf");
    const int semiBold = QFontDatabase::addApplicationFont(":/assets/Inconsolata-SemiBold.ttf");

    const QStringList registered = QFontDatabase::applicationFontFamilies(regular)
                                 + QFontDatabase::applicationFontFamilies(semiBold);

    if (!registered.contains("Inconsolata"))
    {
        qWarning() << "[applyFonts] Inconsolata did not register under the name the stylesheet"
                   << "asks for; got" << registered;
    }

    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setFamilies({"Inconsolata", "Inconsolata SemiBold",
                      "Cascadia Mono", "Consolas", "monospace"});

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

    // The tray menu is the only menu here and it is meant to carry icons. Whether a QAction's icon
    // is drawn at all is a platform decision Qt takes for us - macOS asks for icon-less menus, and
    // so do some Linux desktops - so say it plainly rather than letting the desktop decide, or the
    // mac build comes up with three bare labels.
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus, false);

    // QSettings reads these; without them it files everything under an "Unknown Organization"
    // that moves the moment the binary is renamed.
    QCoreApplication::setOrganizationName("FileDonkey");
    QCoreApplication::setOrganizationDomain("filedonkey.app");
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

    // Before MainWindow, so the application font is in place by the time its widgets are built and
    // none of them has to be re-polished for it.
    applyFonts();

    MainWindow w;

    // A second start hands its job to us rather than running: bring the window back for it.
    QObject::connect(&instance, &SingleInstance::showRequested, &w, &MainWindow::restoreWindow);

    // --tray is what the desktop starts us with when the settings page has asked for FileDonkey at
    // sign-in - see autostart.cpp. Nobody signing in wants a window they did not open, and the
    // application has everything it needs without one: the node runs, the mounts come up, and the
    // tray icon is there to open the window from.
    //
    // Only where there is a tray to sit in. Without one the window is the whole application, and one
    // that never appears is a process the user can neither see nor quit - so on that desktop the
    // flag is ignored rather than obeyed into a corner.
    const bool startInTray = a.arguments().contains(TRAY_ARGUMENT)
                             && QSystemTrayIcon::isSystemTrayAvailable();

    if (!startInTray) w.show();

    return a.exec();
}
