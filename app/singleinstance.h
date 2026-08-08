#ifndef SINGLEINSTANCE_H
#define SINGLEINSTANCE_H

#include <QObject>

QT_BEGIN_NAMESPACE
class QLocalServer;
QT_END_NAMESPACE

// Keeps one FileDonkey per user, and gives the launcher a way to reach the copy already running.
//
// Two things need this. A second process cannot work: the first one holds TCP 5454, and the UDP
// bind uses ShareAddress so the second would happily start answering discovery with the same
// machineId while unable to serve a single request. And once the window can be hidden, clicking
// the launcher is the only way back on a desktop with no system tray - GNOME shows no window for
// a hidden app, so its launcher starts a new process rather than restoring the old one. Turning
// that second start into a "show yourself" for the first is what makes hiding survivable there.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(QObject *parent = nullptr);

    // True when this process took the name and is the one that should carry on. False when
    // another instance already holds it - it has been asked to show its window, and this process
    // has nothing left to do but exit.
    bool claim();

signals:
    // A later start handed its job over to us instead of running.
    void showRequested();

private:
    QLocalServer *server = nullptr;
};

#endif // SINGLEINSTANCE_H
