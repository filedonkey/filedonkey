#include "autostart.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#if defined(Q_OS_WIN)
#include <QSettings>
#endif

namespace {

// This application's own binary, in the form the platform expects to be handed.
QString programPath()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

} // namespace

#if defined(Q_OS_WIN)

// The Run key: what Windows itself offers users through Task Manager's Startup tab, so an entry made
// here is one they can find and turn off where they would look for it. Under HKEY_CURRENT_USER, not
// the machine-wide key beside it - this is one user asking for it, and the machine-wide one needs
// rights the application does not have.
#define RUN_KEY   "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE "FileDonkey"

bool Autostart::isEnabled()
{
    return QSettings(RUN_KEY, QSettings::NativeFormat).contains(RUN_VALUE);
}

void Autostart::setEnabled(bool enabled)
{
    QSettings run(RUN_KEY, QSettings::NativeFormat);

    if (!enabled)
    {
        run.remove(RUN_VALUE);
        return;
    }

    // Quoted, because what the Run key holds is a command line and the path to it usually has a
    // space in it - an unquoted C:\Program Files\... would be read as a program called C:\Program
    // with arguments after it.
    run.setValue(RUN_VALUE, QString("\"%1\" %2").arg(programPath(), TRAY_ARGUMENT));
}

#elif defined(Q_OS_MACOS)

// A launch agent, which is how a user's own login items are written down outside the System
// Settings list. Named after the application the way everything else it files is - see the
// organization domain set in main().
#define AGENT_LABEL "app.filedonkey.FileDonkey"

namespace {

QString agentPath()
{
    return QDir::homePath() + "/Library/LaunchAgents/" + AGENT_LABEL + ".plist";
}

} // namespace

bool Autostart::isEnabled()
{
    return QFile::exists(agentPath());
}

void Autostart::setEnabled(bool enabled)
{
    if (!enabled)
    {
        QFile::remove(agentPath());
        return;
    }

    // launchd reads the agents in this directory at login, so the file being there is the whole
    // request - nothing has to be told about it for the next sign-in to honour it.
    QDir().mkpath(QFileInfo(agentPath()).path());

    QFile agent(agentPath());
    if (!agent.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qDebug() << "[Autostart::setEnabled] could not write the launch agent:" << agent.errorString();
        return;
    }

    const QString plist = QString(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "\t<key>Label</key>\n"
        "\t<string>%1</string>\n"
        "\t<key>ProgramArguments</key>\n"
        "\t<array>\n"
        "\t\t<string>%2</string>\n"
        "\t\t<string>%3</string>\n"
        "\t</array>\n"
        "\t<key>RunAtLoad</key>\n"
        "\t<true/>\n"
        "</dict>\n"
        "</plist>\n").arg(AGENT_LABEL, programPath(), TRAY_ARGUMENT);

    agent.write(plist.toUtf8());
}

#else

// The freedesktop autostart directory, which every desktop this app runs on reads at login.
namespace {

QString desktopEntryPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + "/autostart/filedonkey.desktop";
}

} // namespace

bool Autostart::isEnabled()
{
    return QFile::exists(desktopEntryPath());
}

void Autostart::setEnabled(bool enabled)
{
    if (!enabled)
    {
        QFile::remove(desktopEntryPath());
        return;
    }

    QDir().mkpath(QFileInfo(desktopEntryPath()).path());

    QFile entry(desktopEntryPath());
    if (!entry.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qDebug() << "[Autostart::setEnabled] could not write the autostart entry:" << entry.errorString();
        return;
    }

    // X-GNOME-Autostart-enabled is GNOME's own way of switching an entry off without deleting it.
    // Written true rather than left out, so that an entry a user turned off there and we then wrote
    // again is on rather than quietly still off.
    const QString desktopEntry = QString(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=FileDonkey\n"
        "Exec=\"%1\" %2\n"
        "Terminal=false\n"
        "X-GNOME-Autostart-enabled=true\n").arg(programPath(), TRAY_ARGUMENT);

    entry.write(desktopEntry.toUtf8());
}

#endif
