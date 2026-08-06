// REFERENCES
//
// [linux] https://github.com/libfuse/libfuse/blob/master/example/passthrough.c
// [macos] https://github.com/macos-fuse-t/libfuse/blob/master/example/fusexmp_fh.c
// [win32] https://github.com/winfsp/winfsp/blob/master/tst/passthrough-fuse3/passthrough-fuse3.c

#include "virtdisk.h"
#include "fuseclient.h"
#include "fusebackend_types.h"
#include "tcpkeepalive.h"

#include <cassert>
#include <mutex>
#include <set>
#include <thread>

#define FUSE_USE_VERSION 31

#if defined(__linux__)
#define _XOPEN_SOURCE 700
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QDir>
#include <QProcess>
#include <QTcpSocket>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/mount.h>
#endif

#if defined(__linux__)
#include <spawn.h>
#include <sys/wait.h>

// Declared by unistd.h only under a feature test macro, and _XOPEN_SOURCE above is set too late to
// count - the C++ headers have pulled in features.h by then.
extern char **environ;
#endif

#if defined (_WIN32)
#include <fileapi.h>
#include <winfsp_fuse.h>

#define mkdir(path, mode) _mkdir(path)

#define stat    fuse_stat
#define mode_t  fuse_mode_t
#define statvfs fuse_statvfs
#endif

#if defined(__linux__) || defined(_WIN32)
#define st_atimespec st_atim
#define st_mtimespec st_mtim
#define st_ctimespec st_ctim
#endif

VirtDisk::VirtDisk(const Connection& conn) : conn(conn)
{
    client = new FUSEClient(&this->conn);
}

VirtDisk::~VirtDisk()
{
    // unmount() first: it joins the fuse thread, so no fuse callback can still be holding the
    // client (fuse_new was handed it as private_data) by the time it is freed.
    unmount();

    delete client;
    client = nullptr;
}

static int fd_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    (void)fi;

    qDebug() << "[fd_getattr] path:" << path;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[fd_getattr] FUSEClient not found");

    Ref<GetattrResult> result = client->FD_getattr(path);

    if (result->status == 0)
    {
        stbuf->st_dev = result->st_dev;
        stbuf->st_ino = result->st_ino;
        stbuf->st_nlink = result->st_nlink;
        stbuf->st_mode = result->st_mode;
        stbuf->st_uid = result->st_uid;
        stbuf->st_gid = result->st_gid;
        stbuf->st_rdev = result->st_rdev;
        stbuf->st_size = result->st_size;
        stbuf->st_blksize = result->st_blksize;
        stbuf->st_blocks = result->st_blocks;
        stbuf->st_atimespec.tv_sec = result->st_atim.tv_sec;
        stbuf->st_atimespec.tv_nsec = result->st_atim.tv_nsec;
        stbuf->st_mtimespec.tv_sec = result->st_mtim.tv_sec;
        stbuf->st_mtimespec.tv_nsec = result->st_mtim.tv_nsec;
        stbuf->st_ctimespec.tv_sec = result->st_ctim.tv_sec;
        stbuf->st_ctimespec.tv_nsec = result->st_ctim.tv_nsec;

#if defined (_WIN32)
        if (QString(path) == QString("/")) {
            stbuf->st_mode = 16895;
        }
#endif

        qDebug() << "\tst_atimespec" << stbuf->st_atimespec.tv_sec << stbuf->st_atimespec.tv_nsec;
        qDebug() << "\tst_mtimespec" << stbuf->st_mtimespec.tv_sec << stbuf->st_mtimespec.tv_nsec;
        qDebug() << "\tst_ctimespec" << stbuf->st_ctimespec.tv_sec << stbuf->st_ctimespec.tv_nsec;
        qDebug() << "\tst_blksize" << stbuf->st_blksize;
        qDebug() << "\tst_blocks" << stbuf->st_blocks;
        qDebug() << "\tst_dev" << stbuf->st_dev;
        qDebug() << "\tst_gid" << stbuf->st_gid;
        qDebug() << "\tst_ino" << stbuf->st_ino;
        qDebug() << "\tst_mode" << stbuf->st_mode;
        qDebug() << "\tst_nlink" << stbuf->st_nlink;
        qDebug() << "\tst_rdev" << stbuf->st_rdev;
        qDebug() << "\tst_size" << stbuf->st_size;
        qDebug() << "\tst_uid" << stbuf->st_uid;
        qDebug() << "\t";
    }

    return result->status;
}

static int fd_readlink(const char *path, char *buf, size_t size)
{
    qDebug() << "[fd_readlink] path: " << path;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[fd_readlink] FUSEClient not found");

    Ref<ReadlinkResult> result = client->FD_readlink(path, size);

    if (result->status == 0)
    {
        memcpy(buf, result->data, result->size);
    }

    return result->status;
}

static int fd_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi,
                       enum fuse_readdir_flags flags)
{
    (void)offset;
    (void)fi;
    (void)flags;

    qDebug() << "[fd_readdir] path: " << path;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[fd_readdir] FUSEClient not found");

    Ref<ReaddirResult> result = client->FD_readdir(path);

    struct stat st;
    memset(&st, 0, sizeof(st));

    qDebug() << "before for";
    for (unsigned int i = 0; i < result->count; ++i)
    {
        qDebug() << "for i" << i;
        FindData *fd = (FindData *)result->findData + i;
        qDebug() << "[fd_readdir] incoming findData name:" << fd->name;
        qDebug() << "[fd_readdir] incoming findData st_ino:" << fd->st_ino;
        qDebug() << "[fd_readdir] incoming findData st_mode:" << fd->st_mode;

        st.st_ino = fd->st_ino;
        st.st_mode = fd->st_mode;
#if defined (_WIN32)
        st.st_size = 146;
        st.st_blksize = 4096;
        st.st_blocks = 2;
        st.st_atim.tv_sec = 1763752599;
        st.st_atim.tv_nsec = 302761200;
        st.st_mtim.tv_sec = 1747514473;
        st.st_mtim.tv_nsec = 21076073;
        st.st_ctim.tv_sec = 1747514473;
        st.st_ctim.tv_nsec = 21076073;
#endif

        filler(buf, fd->name, &st, 0, fuse_fill_dir_flags::FUSE_FILL_DIR_PLUS);
    }
    qDebug() << "after for";

    int status = result->status;

    return status;
}

static int fd_mkdir(const char *path, mode_t mode)
{
    qDebug() << "[fd_mkdir] path: " << path;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[fd_mkdir] FUSEClient not found");

    Ref<StatusResult> result = client->FD_mkdir(path, mode);

    return result->status;
}

static int fd_unlink(const char *path)
{
    qDebug() << "[fd_unlink] path: " << path;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[fd_unlink] FUSEClient not found");

    Ref<StatusResult> result = client->FD_unlink(path);

    return result->status;
}

static int fd_rmdir(const char *path)
{
    qDebug() << "[fd_rmdir] path: " << path;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[fd_rmdir] FUSEClient not found");

    Ref<StatusResult> result = client->FD_rmdir(path);

    return result->status;
}

static int fd_rename(const char *from, const char *to, unsigned int flags)
{
    qDebug() << "[fd_rename] from: " << from << "to:" << to;

    if (flags) return -EINVAL;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[fd_rename] FUSEClient not found");

    Ref<StatusResult> result = client->FD_rename(from, to);

    return result->status;
}

static int fd_truncate(const char *path, off_t size, struct fuse_file_info *fi)
{
    (void)fi;

    qDebug() << "[fd_truncate] path: " << path << "size:" << size;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[fd_truncate] FUSEClient not found");

    Ref<StatusResult> result = client->FD_truncate(path, size);

    return result->status;
}

static int fd_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    qDebug() << "[fd_create] path: " << path;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[fd_create] FUSEClient not found");

    qDebug() << "[fd_create] flags:" << fi->flags;
    qDebug() << "[fd_create] mode:" << mode;

#if defined (_WIN32)
    Ref<StatusResult> result = client->FD_create(path, 33188, 32961);
#else
    Ref<StatusResult> result = client->FD_create(path, mode, fi->flags);
#endif

    qDebug() << "[fd_create] status: " << result->status;

    return result->status;
}

static int fd_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
    (void)fi;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[fd_read] FUSEClient not found");

    Ref<ReadResult> result = client->FD_read(path, size, offset);

    memset(buf, 0, size);

    if (result->status > 0) {
        memcpy(buf, result->data, result->status);
    }

    return result->status;
}

static int fd_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
{
    (void)fi;

    qDebug() << "[fd_write] path: " << path;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[fd_write] FUSEClient not found");

    Ref<StatusResult> result = client->FD_write(path, buf, size, offset);

    qDebug() << "[fd_write] incoming result status:" << result->status;

    return result->status;
}

static int fd_statfs(const char *path, struct statvfs *stbuf)
{
    qDebug() << "[fd_statfs] path: " << path;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[fd_statfs] FUSEClient not found");

    Ref<StatfsResult> result = client->FD_statfs(path);

    if (result->status == 0)
    {
        stbuf->f_bsize = result->f_bsize;
        stbuf->f_frsize = result->f_frsize;
        stbuf->f_blocks = result->f_blocks;
        stbuf->f_bfree = result->f_bfree;
        stbuf->f_bavail = result->f_bavail;
        stbuf->f_files = result->f_files;
        stbuf->f_ffree = result->f_ffree;
        stbuf->f_favail = result->f_favail;
        stbuf->f_fsid = result->f_fsid;
        stbuf->f_flag = result->f_flag;
        stbuf->f_namemax = result->f_namemax;
    }

    return result->status;
}

static int fd_open(const char *path, struct fuse_file_info *fi)
{
    (void)fi;

    qDebug() << "[fd_open] path: " << path;

    return 0;
}

static const struct fuse_operations fd_oper = {
    .getattr	= fd_getattr,
    .readlink	= fd_readlink,
    .mkdir		= fd_mkdir,
    .unlink		= fd_unlink,
    .rmdir		= fd_rmdir,
    .rename		= fd_rename,
    .truncate	= fd_truncate,
    .open		= fd_open,
    .read		= fd_read,
    .write		= fd_write,
    .statfs		= fd_statfs,
    .readdir	= fd_readdir,
    .create		= fd_create,
};

#if defined (_WIN32)
// Letters handed to mount threads that have not finished yet. Every peer gets a mount thread and
// on startup they all begin together, so two of them pick a drive letter at about the same moment.
// GetLogicalDrives() alone is not enough to keep them apart: fuse_mount() only records the mount
// point, WinFsp does not take the letter until fuse_loop() starts the file system, so the letter
// still reads as free for as long as it takes the other thread to get there. Both threads then
// choose it and the second one dies in fuse_loop with "Cannot set WinFsp-FUSE file system mount
// point" / STATUS_OBJECT_NAME_COLLISION (c0000035). Our own record covers that window; it is kept
// until the mount is torn down and the letter is genuinely gone from GetLogicalDrives() again.
static std::mutex driveLettersMutex;
static std::set<char> reservedDriveLetters;

static char ReserveFreeDriveLetter()
{
    std::lock_guard<std::mutex> lock(driveLettersMutex);

    DWORD mask = GetLogicalDrives();

    for (char drive = 'D'; drive <= 'Z'; ++drive)
    {
        if (mask & (1u << (drive - 'A'))) continue;
        if (reservedDriveLetters.count(drive)) continue;

        reservedDriveLetters.insert(drive);
        return drive;
    }

    return '\0'; // no free drive letter
}

// Hands the letter back on every path out of StartImpl, mount failures included.
struct DriveLetterReservation
{
    char letter = '\0';

    ~DriveLetterReservation()
    {
        if (!letter) return;

        std::lock_guard<std::mutex> lock(driveLettersMutex);
        reservedDriveLetters.erase(letter);
    }
};
#endif

#if !defined(_WIN32)
// Never through a shell: the last component of the mount point is the peer's machine name, and that
// comes off a broadcast anyone on the network can send. system() would take a peer announcing
// itself as "x; rm -rf ~" at its word.
static void UnmountAt(const std::string &mountpoint)
{
#if defined(__APPLE__)
    // What diskutil would end up calling anyway: fuse-t serves the volume over NFS, so there is no
    // disk behind it for DiskArbitration to do anything more about. Doing it here instead of in a
    // child process also keeps us clear of fuse-t's SIGCHLD handler, which swallows the exit
    // notification QProcess::execute() waits on - that leaves the helper stuck on a zombie forever.
    // Forced because every caller is on a teardown path and the far side is already gone.
    unmount(mountpoint.c_str(), MNT_FORCE);
#elif defined(__linux__)
    // No syscall to use here: umount2() wants root, and fusermount3 is the setuid helper that lets
    // whoever mounted it take it back down. Spawned directly, and reaped here rather than through
    // QProcess, for the same reason as above.
    const char *argv[] = {"fusermount3", "-u", mountpoint.c_str(), nullptr};

    pid_t pid = 0;
    if (posix_spawnp(&pid, argv[0], nullptr, nullptr, (char *const *)argv, environ) != 0) return;

    waitpid(pid, nullptr, 0);
#endif
}
#endif

static void StartImpl(VirtDisk *self, Connection *conn)
{
    self->socket = new QTcpSocket();

    QObject::connect(self->socket, &QTcpSocket::stateChanged, self, &VirtDisk::onSocketStateChanged);
    // QObject::connect(self->socket, &QTcpSocket::disconnected, self, &VirtDisk::onSocketDisconnected);

    QObject::connect(self->socket, &QTcpSocket::disconnected, self, [self]() {
        qDebug() << "[Start] socket disconnected";
        if (self->f) fuse_exit(self->f);
    });

    qDebug() << "[Start] try to connect";
    self->socket->connectToHost(QHostAddress(conn->machineAddress), conn->machinePort);
    if (!self->socket->waitForConnected())
    {
        qDebug() << "[Start] socket connection error:" << self->socket->errorString();
        return;
    }
    qDebug() << "[Start] socket connected";

    if (!setTcpKeepAlive(self->socket, 3, 3, 3))
    {
        qDebug() << "[Start] failed to configure TCP keepalive";
    }

    self->socket->setSocketOption(QAbstractSocket::LowDelayOption,  1);

    // Only now, so a mount whose dial failed leaves the client without a socket and every Fetch
    // fails fast instead of reaching for one that was never connected.
    self->client->setSocket(self->socket);

    std::string mount_name_option;

#if defined(__APPLE__) || defined (_WIN32)
    mount_name_option = QString("volname=%1").arg(conn->machineName).toStdString();
#elif defined(__linux__)
    mount_name_option = QString("fsname=%1").arg(conn->machineName).toStdString();
#endif

    char *argv[] = {
        "FileDonkey",
        "-o", mount_name_option.data(),
        "-o", "subtype=filedonkey",
        "-o", "auto_cache"
#if defined (_WIN32)
        // Own every file as whoever is logged in here. Without this WinFsp maps the peer's own
        // uid and gid - 501 and 20 off a Mac - onto SIDs that are nobody on this machine, so the
        // user matches only the "other" triple. WinFsp grants that triple FILE_WRITE_DATA but not
        // FILE_WRITE_EA, and GENERIC_WRITE asks for both, so every editor that opens a file for
        // writing the ordinary way is refused before the request ever reaches us. The owner ACE
        // does carry FILE_WRITE_EA; this is what makes us the owner.
        ,"-o", "uid=-1,gid=-1"
#endif
    };

    int argc = sizeof(argv) / sizeof(argv[0]);
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    self->f = fuse_new(&args, &fd_oper, sizeof(fd_oper), self->client);
    if (!self->f)
    {
        qDebug() << "[Start] fuse_new failed";
        fuse_opt_free_args(&args);
        return;
    }

    struct fuse_session *se = fuse_get_session(self->f);

#if defined (_WIN32)
    // Held until this function returns, i.e. until the mount is torn down again.
    DriveLetterReservation reservation;
    reservation.letter = ReserveFreeDriveLetter();

    char driveLetter = reservation.letter;
    if (!driveLetter)
    {
        qDebug() << "[Start] can't find a free drive letter";
        fuse_destroy(self->f);
        self->f = nullptr;
        fuse_opt_free_args(&args);
        return;
    }
    self->mountpoint = QString("%1:").arg(driveLetter).toStdString();
    qDebug() << "[Start] mounting" << conn->machineName << "on" << self->mountpoint.c_str()
             << "drives in use:" << Qt::hex << GetLogicalDrives();
#else
    // Mount inside a hidden directory, never straight into the home directory. FUSEBackend
    // exports $HOME and its readdir skips dotfiles, so this keeps our own mounts of the other
    // peers out of what we serve. Mounted at $HOME/<machineName> they were listed like ordinary
    // folders, and a peer browsing our disk would walk into them - we would then proxy its reads
    // on to the machine behind that mount, so the same file got read off two machines at once.
    QString base = QDir::homePath() + "/.filedonkey";
    mkdir(base.toStdString().c_str(), 0755);

    self->mountpoint = QString("%1/%2").arg(base).arg(conn->machineName).toStdString();
    qDebug() << "[Start] mountpoint directory:" << self->mountpoint.c_str();

    int mntdir_rc = mkdir(self->mountpoint.c_str(), 0755);
    if (mntdir_rc == -1) {
        qDebug() << "[Start] could'n create a mountpoint directory";

        UnmountAt(self->mountpoint);

        rmdir(self->mountpoint.c_str());
        mkdir(self->mountpoint.c_str(), 0755);
    }
    qDebug() << "[Start] mntdir_rc rc:" << mntdir_rc << "errno:" << errno;
#endif // _WIN32

    if (fuse_mount(self->f, self->mountpoint.c_str()) != 0)
    {
        qDebug() << "[Start] fuse_mount failed";
        fuse_destroy(self->f);
        self->f = nullptr;
        fuse_opt_free_args(&args);
        return;
    }

    if (fuse_set_signal_handlers(se) != 0)
    {
        qDebug() << "[Start] fuse_set_signal_handlers failed";
        fuse_unmount(self->f);
        fuse_destroy(self->f);
        self->f = nullptr;
        fuse_opt_free_args(&args);
        return;
    }

    int rc = fuse_loop(self->f);
    qDebug() << "[Start] fuse_loop returned" << rc;

    fuse_remove_signal_handlers(se);
    fuse_unmount(self->f);
    fuse_destroy(self->f);
    fuse_opt_free_args(&args);

    self->f = nullptr;
}

static void Start(VirtDisk *self, Connection *conn)
{
    StartImpl(self, conn);

    // Closing our end is what makes the teardown symmetric: the peer's server notices the drop on
    // its event loop and stops the VirtDisk facing us. Leaving it open would keep the connection
    // established after our mount is gone, so the peer would never learn we went away. The socket
    // was created on this thread and no Fetch can be in flight now that fuse_loop has returned,
    // so this is the right place to destroy it.
    if (self->socket)
    {
        self->client->setSocket(nullptr);
        self->socket->close();
        delete self->socket;
        self->socket = nullptr;
    }

    // Queued to whoever owns this VirtDisk, so they can join and delete it now that the mount is
    // gone. This thread is about to return, so that join costs nothing. Emitting from the wrapper
    // rather than inside StartImpl covers its failure returns too - a VirtDisk that never got as
    // far as mounting must still be cleaned up, or its peer can never be rediscovered.
    emit self->stopped();
}

void VirtDisk::onSocketDisconnected()
{
    qDebug() << "[VirtDisk::onSocketDisconnected] disconnect socket";
    unmount();
}

void VirtDisk::onSocketStateChanged(QAbstractSocket::SocketState socketState)
{
    qDebug() << "[VirtDisk::onSocketStateChanged] new state:" << socketState;
}

void VirtDisk::mount(const QString &mountPoint)
{
#if defined(__APPLE__)
    // fuse-t serialises every session in a process on one global mutex (darwin_read_lock) and
    // holds it across the blocking receive, so whichever session reads first keeps it forever
    // and a second mount here would never be served - it just hangs in Finder. Give each mount
    // a process of its own. Only the mount moves out: discovery and the inbound server that
    // answers this peer's requests stay here.
    worker = new QProcess(this);
    worker->setProgram(QCoreApplication::applicationFilePath());
    worker->setArguments({"--mount",
                          conn.machineId,
                          conn.machineName,
                          conn.machineAddress,
                          QString::number(conn.machinePort)});

    // Its stdout is the stats channel; its qDebug goes to stderr and joins ours.
    worker->setProcessChannelMode(QProcess::ForwardedErrorChannel);

    connect(worker, &QProcess::readyReadStandardOutput, this, &VirtDisk::onWorkerOutput);

    connect(worker, &QProcess::finished, this, [this]() {
        qDebug() << "[VirtDisk::mount] mount helper exited for:" << conn.machineName;
        emit stopped();
    });

    // finished() never comes if the helper could not be launched at all, and without a stopped()
    // our owner would keep this VirtDisk and its connections entry forever - the peer could then
    // never be rediscovered. Report it as a stop like any other.
    connect(worker, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        qDebug() << "[VirtDisk::mount] mount helper failed to start:" << worker->errorString();
        emit stopped();
    });

    worker->start();
#else
    thread = std::thread(Start, this, &conn);
#endif
}

int VirtDisk::runMountWorker()
{
    // The parent has no socket of its own for this peer, so hand it our running totals for the
    // UI. Throttled: FUSEClient reports on every request, and a write per FUSE call would sit
    // right on the read path.
    u64 up = 0, down = 0;
    QElapsedTimer sinceReport;
    sinceReport.start();

    auto report = [&](bool force) {
        if (!force && sinceReport.elapsed() < 250) return;
        sinceReport.restart();
        printf("@stats %llu %llu\n", (unsigned long long)up, (unsigned long long)down);
        fflush(stdout);
    };

    connect(client, &FUSEClient::uploadedChanged,   this, [&](u64 value) { up = value;   report(false); });
    connect(client, &FUSEClient::downloadedChanged, this, [&](u64 value) { down = value; report(false); });

    Start(this, &conn);

    // Before the lambdas' captures go out of scope.
    disconnect(client, nullptr, this, nullptr);

    report(true);

    return 0;
}

#if defined(__APPLE__)
void VirtDisk::onWorkerOutput()
{
    while (worker->canReadLine())
    {
        const QByteArray line = worker->readLine().trimmed();
        if (!line.startsWith("@stats ")) continue;

        const QList<QByteArray> parts = line.split(' ');
        if (parts.size() != 3) continue;

        client->setTransferred(parts[1].toULongLong(), parts[2].toULongLong());
    }
}

// SIGTERM, which libfuse's own signal handler turns into a session exit - the helper then unmounts
// and cleans up on its way out, and we hear about it through finished().
//
// Only ever once, though. That handler is uninstalled by fuse_remove_signal_handlers() the moment
// the loop ends, which puts SIGTERM back on its default disposition, so a second one arrives as a
// plain kill and takes the helper out in the middle of its teardown - before it has unmounted and
// removed its own mount point, which is then left behind. Everyone who tears a VirtDisk down asks
// twice or more: MainWindow calls stop() when the peer's socket drops and again from its
// destructor, and unmount() has to ask too for the paths that never went through stop().
void VirtDisk::terminateWorker()
{
    if (!worker || terminatedWorker) return;
    if (worker->state() == QProcess::NotRunning) return;

    terminatedWorker = true;
    worker->terminate();
}
#endif

void VirtDisk::stop()
{
    qDebug() << "[VirtDisk::stop] asking the fuse loop to exit";

#if defined(__APPLE__)
    if (worker)
    {
        terminateWorker();
        return;
    }
#endif

    if (f) fuse_exit(f);

#if defined(__linux__)
    unmountLinux();
#endif
}

#if defined(__linux__)
// fuse_exit only raises a flag, and fuse_loop is blocked in a read on /dev/fuse that will not look
// at it until the next filesystem request arrives - for a mount nobody is touching, that can be
// never. Unmounting is what actually wakes it: the kernel closes the channel and the read returns.
// macOS gets this for free because the helper is stopped with a signal, which interrupts the read;
// on Linux nobody sends one. Without it the mount lingers with a dead socket behind it (ls reports
// ECONNABORTED, since our handlers are still answering), the fuse thread never emits stopped(),
// and so the peer is never removed from connections - when it comes back its broadcast is ignored
// and it is never remounted.
//
// Both stop() and unmount() need that wake-up and either can run first, so this only does it once:
// asking fusermount3 to unmount a path that is already gone earns an /etc/mtab complaint on
// stderr. Both callers are on the GUI thread, so the flag needs no guarding.
void VirtDisk::unmountLinux()
{
    if (mountpoint.empty() || unmountedLinux) return;

    unmountedLinux = true;
    UnmountAt(mountpoint);
}
#endif

void VirtDisk::unmount()
{
#if defined(__APPLE__)
    if (worker)
    {
        terminateWorker();

        // Give it room to unmount and remove its own mount point before we go; a kill here is a
        // last resort, and it costs exactly the cleanup we are waiting for.
        if (worker->state() != QProcess::NotRunning && !worker->waitForFinished(5000)) worker->kill();

        // The helper unmounted and removed its own mount point; nothing left for us to do.
        return;
    }
#endif

    if (f) fuse_exit(f);

#if defined(__linux__)
    // Before the join, never after. The join waits for fuse_loop to return, and on Linux the
    // unmount is the only thing that makes it return - see unmountLinux(). Unmounting afterwards,
    // as this used to, is a deadlock: we wait for a loop that is waiting for the unmount we have
    // not done yet.
    unmountLinux();
#endif

    if (thread.joinable()) thread.join();

#if defined(__APPLE__)
    UnmountAt(mountpoint);
    rmdir(mountpoint.c_str());
#elif defined(__linux__)
    rmdir(mountpoint.c_str());
#endif

    qDebug() << "unmounted";
}
