#ifndef VIRTDISK_H
#define VIRTDISK_H

#include "connection.h"
#include "fuseclient.h"

#include <QProcess>
#include <QString>
#include <QTcpSocket>

#include <thread>

class VirtDisk : public QObject
{
    Q_OBJECT

public:
    VirtDisk(const Connection& conn);
    ~VirtDisk();

    void mount(const QString &mountPoint);

    // Asks the fuse loop to exit and returns immediately. Unlike unmount() this never joins the
    // thread, so it is safe to call from the GUI thread while the fuse thread sits in a Fetch
    // that has not timed out yet. Wait for stopped() before destroying the VirtDisk.
    void stop();

    // Runs the mount on the calling thread and returns once the fuse loop is done. This is what
    // the macOS helper process does; see mount() for why that process exists. Elsewhere mount()
    // runs the same code on a thread of its own and nothing calls this.
    int runMountWorker();

    FUSEClient *client;

    QTcpSocket *socket = nullptr;

    struct fuse *f = nullptr;
    std::string mountpoint;

signals:
    // Emitted from the fuse thread the moment the mount is up, carrying the place it landed: a
    // drive letter on Windows, a directory under ~/.filedonkey elsewhere. VirtDisk chooses that
    // itself - see ReserveFreeDriveLetter - so this is the only way anyone else learns of it.
    // A mount that never comes up emits stopped() and nothing else.
    void mounted(const QString &mountPoint);

    // Emitted from the fuse thread once fuse_loop has returned and the mount is torn down, and
    // from the GUI thread for the ways a mount can end before there is a thread to report it.
    //
    // The reason is empty for an ordinary teardown - the peer went away, or we are shutting down -
    // and a sentence fit to show when the mount could not be brought up at all. Whoever owns this
    // VirtDisk has to tell the two apart: a peer that merely went is forgotten and found again on
    // the next broadcast, while one whose mount failed will fail again the same way in five
    // seconds, and did, for as long as this signal carried nothing.
    void stopped(const QString &reason);

public slots:
    void onSocketDisconnected();
    void onSocketStateChanged(QAbstractSocket::SocketState socketState);

private:
    void unmount();

#if defined(__linux__)
    // Brings the mount down, which is also how a blocked fuse_loop is woken. Runs at most once.
    void unmountLinux();

    bool unmountedLinux = false;
#endif

#if defined(__APPLE__)
    // Reads the transfer totals the helper reports on its stdout, so the UI counters keep
    // working now that the socket doing the transferring lives in another process. The helper's
    // verdict on the mount comes back the same way - see workerFailure below.
    void onWorkerOutput();

    // Asks the mount helper to come down. Runs at most once.
    void terminateWorker();

    QProcess *worker = nullptr;
    bool terminatedWorker = false;

    // Why the helper's mount never came up, as it reported it on stdout, held until the process
    // exits and stopped() can carry it out. Empty for a mount that ran and was stopped, which is
    // the same thing empty means on that signal.
    QString workerFailure;
#endif

    QString mountPoint;
    Connection conn;
    std::thread thread;
};

#endif // VIRTDISK_H
