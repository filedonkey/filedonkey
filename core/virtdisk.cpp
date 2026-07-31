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

#include <QHostAddress>
#include <QDir>
#include <QTcpSocket>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/mount.h>
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
    unmount();
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
static char FindFreeDriveLetter()
{
    DWORD mask = GetLogicalDrives();

    for (char drive = 'D'; drive <= 'Z'; ++drive)
    {
        if ((mask & (1u << (drive - 'A'))) == 0)
            return drive;
    }

    return '\0'; // no free drive letter
}
#endif

static void Start(VirtDisk *self, Connection *conn)
{
    conn->socket = new QTcpSocket();

    QObject::connect(conn->socket, &QTcpSocket::stateChanged, self, &VirtDisk::onSocketStateChanged);
    // QObject::connect(conn->socket, &QTcpSocket::disconnected, self, &VirtDisk::onSocketDisconnected);

    QObject::connect(conn->socket, &QTcpSocket::disconnected, [self]() {
        qDebug() << "[Start] socket disconnected";
        fuse_exit(self->f);
    });

    qDebug() << "[Start] try to connect";
    conn->socket->connectToHost(QHostAddress(conn->machineAddress), conn->machinePort);
    if (!conn->socket->waitForConnected())
    {
        qDebug() << "[Start] socket connection error:" << conn->socket->errorString();
        return;
    }
    qDebug() << "[Start] socket connected";

    if (!setTcpKeepAlive(conn->socket, 3, 3, 3))
    {
        qDebug() << "[Start] failed to configure TCP keepalive";
    }

    conn->socket->setSocketOption(QAbstractSocket::LowDelayOption,  1);

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
    char driveLetter = FindFreeDriveLetter();
    if (!driveLetter)
    {
        qDebug() << "[Start] can't find a free drive letter";
        return;
    }
    self->mountpoint = QString("%1:").arg(driveLetter).toStdString();
#else
    QString base = QDir::homePath();

#if defined(__APPLE__)
    base += "/.filedonkey";
    mkdir(base.toStdString().c_str(), 0755);
#endif

    self->mountpoint = QString("%1/%2").arg(base).arg(conn->machineName).toStdString();
    qDebug() << "[Start] mountpoint directory:" << self->mountpoint.c_str();

    int mntdir_rc = mkdir(self->mountpoint.c_str(), 0755);
    if (mntdir_rc == -1) {
        qDebug() << "[Start] could'n create a mountpoint directory";

#if defined(__APPLE__)
        system(QString("diskutil unmount %1").arg(self->mountpoint.c_str()).toStdString().c_str());
#elif defined(__linux__)
        system(QString("fusermount3 -u %1").arg(self->mountpoint.c_str()).toStdString().c_str());
#endif

        rmdir(self->mountpoint.c_str());
        mkdir(self->mountpoint.c_str(), 0755);
    }
    qDebug() << "[Start] mntdir_rc rc:" << mntdir_rc << "errno:" << errno;
#endif // _WIN32

    if (fuse_mount(self->f, self->mountpoint.c_str()) != 0)
    {
        qDebug() << "[Start] fuse_mount failed";
        fuse_destroy(self->f);
        fuse_opt_free_args(&args);
        return;
    }

    if (fuse_set_signal_handlers(se) != 0)
    {
        qDebug() << "[Start] fuse_set_signal_handlers failed";
        fuse_unmount(self->f);
        fuse_destroy(self->f);
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
    thread = std::thread(Start, this, &conn);
}

void VirtDisk::unmount()
{
    fuse_exit(f);
    thread.join();

#if defined(__APPLE__)
    system(QString("diskutil unmount %1").arg(mountpoint.c_str()).toStdString().c_str());
    rmdir(mountpoint.c_str());
#elif defined(__linux__)
    system(QString("fusermount3 -u %1").arg(mountpoint.c_str()).toStdString().c_str());
    rmdir(mountpoint.c_str());
#endif

    qDebug() << "unmounted";
}
