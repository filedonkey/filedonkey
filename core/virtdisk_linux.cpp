#if defined(__linux__)

// Example source:
// https://github.com/libfuse/libfuse/blob/master/example/passthrough.c

#include "virtdisk.h"
#include "fuseclient.h"
#include "fusebackend_types.h"

#include <cassert>
#include <thread>

#define FUSE_USE_VERSION 31

#ifdef linux
/* For pread()/pwrite()/utimensat() */
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

#include <QTcpSocket>

VirtDisk::VirtDisk(const Connection& conn) : conn(conn), client(new FUSEClient(&this->conn))
{
}

VirtDisk::~VirtDisk()
{
    unmount();
}

static int xmp_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    (void)fi;

    qDebug() << "[xmp_getattr] path:" << path;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[xmp_getattr] FUSEClient not found");

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
        stbuf->st_atim.tv_sec = result->st_atim.tv_sec;
        stbuf->st_atim.tv_nsec = result->st_atim.tv_nsec;
        stbuf->st_mtim.tv_sec = result->st_mtim.tv_sec;
        stbuf->st_mtim.tv_nsec = result->st_mtim.tv_nsec;
        stbuf->st_ctim.tv_sec = result->st_ctim.tv_sec;
        stbuf->st_ctim.tv_nsec = result->st_ctim.tv_nsec;

        qDebug() << "\tst_atimespec" << stbuf->st_atim.tv_sec << stbuf->st_atim.tv_nsec;
        // qDebug() << "\tst_birthtimespec" << stbuf->st_birthtim.tv_sec << stbuf->st_birthtim.tv_nsec;
        qDebug() << "\tst_blksize" << stbuf->st_blksize;
        qDebug() << "\tst_blocks" << stbuf->st_blocks;
        qDebug() << "\tst_ctimespec" << stbuf->st_ctim.tv_sec << stbuf->st_ctim.tv_nsec;
        qDebug() << "\tst_dev" << stbuf->st_dev;
        qDebug() << "\tst_gid" << stbuf->st_gid;
        qDebug() << "\tst_ino" << stbuf->st_ino;
        qDebug() << "\tst_mode" << stbuf->st_mode;
        qDebug() << "\tst_mtimespec" << stbuf->st_mtim.tv_sec << stbuf->st_mtim.tv_nsec;
        qDebug() << "\tst_nlink" << stbuf->st_nlink;
        qDebug() << "\tst_rdev" << stbuf->st_rdev;
        qDebug() << "\tst_size" << stbuf->st_size;
        qDebug() << "\tst_uid" << stbuf->st_uid;
        qDebug() << "\t";
    }

    return result->status;
}

static int xmp_access(const char *path, int mask)
{
    qDebug() << "[xmp_access] path: " << path;

    return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
    qDebug() << "[xmp_readlink] path: " << path;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[xmp_readlink] FUSEClient not found");

    Ref<ReadlinkResult> result = client->FD_readlink(path, size);

    if (result->status == 0)
    {
        memcpy(buf, result->data, result->size);
    }

    return result->status;
}


static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi,
                       enum fuse_readdir_flags flags)
{
    (void)offset;
    (void)fi;
    (void)flags;

    qDebug() << "[xmp_readdir] path: " << path;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[xmp_readdir] FUSEClient not found");

    Ref<ReaddirResult> result = client->FD_readdir(path);

    struct stat st;
    memset(&st, 0, sizeof(st));

    qDebug() << "before for";
    for (unsigned int i = 0; i < result->count; ++i)
    {
        qDebug() << "for i" << i;
        FindData *fd = (FindData *)result->findData + i;
        qDebug() << "[xmp_readdir] incoming findData name:" << fd->name;
        qDebug() << "[xmp_readdir] incoming findData st_ino:" << fd->st_ino;
        qDebug() << "[xmp_readdir] incoming findData st_mode:" << fd->st_mode;

        st.st_ino = fd->st_ino;
        st.st_mode = fd->st_mode;
        // st.st_size = 146;
        // st.st_blksize = 4096;
        // st.st_blocks = 2;
        // st.st_atim.tv_sec = 1763752599;
        // st.st_atim.tv_nsec = 302761200;
        // st.st_mtim.tv_sec = 1747514473;
        // st.st_mtim.tv_nsec = 21076073;
        // st.st_ctim.tv_sec = 1747514473;
        // st.st_ctim.tv_nsec = 21076073;

        filler(buf, fd->name, &st, /*nextoff*/0, fuse_fill_dir_flags::FUSE_FILL_DIR_PLUS);
    }
    qDebug() << "after for";

    int status = result->status;

    return status;
}

static int xmp_mkdir(const char *path, mode_t mode)
{
    qDebug() << "[xmp_mkdir] path: " << path;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[xmp_mkdir] FUSEClient not found");

    Ref<StatusResult> result = client->FD_mkdir(path, mode);

    return result->status;
}

static int xmp_unlink(const char *path)
{
    qDebug() << "[xmp_unlink] path: " << path;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[xmp_unlink] FUSEClient not found");

    Ref<StatusResult> result = client->FD_unlink(path);

    return result->status;
}

static int xmp_rmdir(const char *path)
{
    qDebug() << "[xmp_rmdir] path: " << path;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[xmp_rmdir] FUSEClient not found");

    Ref<StatusResult> result = client->FD_rmdir(path);

    return result->status;
}

static int xmp_rename(const char *from, const char *to, unsigned int flags)
{

    qDebug() << "[xmp_rename] from: " << from << "to:" << to;

    if (flags) return -EINVAL;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[xmp_rename] FUSEClient not found");

    Ref<StatusResult> result = client->FD_rename(from, to);

    return result->status;
}

static int xmp_truncate(const char *path, off_t size, struct fuse_file_info *fi)
{
    (void)fi;

    qDebug() << "[xmp_truncate] path: " << path << "size:" << size;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[xmp_truncate] FUSEClient not found");

    Ref<StatusResult> result = client->FD_truncate(path, size);

    return result->status;
}

static int xmp_create(const char *path, mode_t mode,
                      struct fuse_file_info *fi)
{
    qDebug() << "[xmp_create] path: " << path;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[xmp_create] FUSEClient not found");

    qDebug() << "[xmp_create] flags:" << fi->flags;
    qDebug() << "[xmp_create] mode:" << mode;

    Ref<StatusResult> result = client->FD_create(path, mode, fi->flags);

    qDebug() << "[xmp_create] status: " << result->status;

    return result->status;
}

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_open] path: " << path;

    return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
    (void)fi;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[xmp_read] FUSEClient not found");

    Ref<ReadResult> result = client->FD_read(path, size, offset);

    memset(buf, 0, size);

    if (result->status > 0) {
        memcpy(buf, result->data, result->status);
    }

    return result->status;
}

static int xmp_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
{
    (void)fi;

    qDebug() << "[xmp_write] path: " << path;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[xmp_write] FUSEClient not found");

    Ref<StatusResult> result = client->FD_write(path, buf, size, offset);

    qDebug() << "[xmp_write] incoming result status:" << result->status;

    return result->status;
}

static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
    qDebug() << "[xmp_statfs] path: " << path;

    FUSEClient *client = (FUSEClient *)fuse_get_context()->private_data;
    assert(client && "[xmp_statfs] FUSEClient not found");

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

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_release] path: " << path;

    /* Just a stub.	 This method is optional and can safely be left
       unimplemented */

    (void) path;
    (void) fi;
    return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
                     struct fuse_file_info *fi)
{
    qDebug() << "[xmp_fsync] path: " << path;

    /* Just a stub.	 This method is optional and can safely be left
       unimplemented */

    (void) path;
    (void) isdatasync;
    (void) fi;
    return 0;
}

static const struct fuse_operations xmp_oper = {
    // Minimal v1 operation set.
    .getattr	= xmp_getattr,
    .readlink	= xmp_readlink,
    .mkdir		= xmp_mkdir,
    .unlink		= xmp_unlink,
    .rmdir		= xmp_rmdir,
    .rename		= xmp_rename,
    .truncate	= xmp_truncate,
    .open		= xmp_open,
    .read		= xmp_read,
    .write		= xmp_write,
    .statfs		= xmp_statfs,
    .release	= xmp_release,
    .fsync		= xmp_fsync,
    .readdir	= xmp_readdir,
    .access		= xmp_access,
    .create		= xmp_create,
};

static void Start(VirtDisk *self, Connection *conn)
{
    conn->socket = new QTcpSocket();

    QObject::connect(conn->socket, &QTcpSocket::stateChanged, self, &VirtDisk::onSocketStateChanged);
    QObject::connect(conn->socket, &QTcpSocket::disconnected, self, &VirtDisk::onSocketDisconnected);

    qDebug() << "[Start] try to connect";
    conn->socket->connectToHost(QHostAddress(conn->machineAddress), conn->machinePort);
    if (!conn->socket->waitForConnected())
    {
        qDebug() << "[Start] socket connection error:" << conn->socket->errorString();
        return;
    }
    qDebug() << "[Start] socket connected";

    conn->socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    conn->socket->setSocketOption(QAbstractSocket::LowDelayOption,  1);

    std::string fsname = QString("fsname=%1").arg(conn->machineName).toStdString();

    char *argv[] = {"FileDonkey", "-o", fsname.data()};
    int argc = sizeof(argv) / sizeof(argv[0]);
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    const char* runtime = getenv("XDG_RUNTIME_DIR");
    system(QString("mkdir -p %1/%2")
               .arg(runtime)
               .arg(conn->machineName)
               .toStdString().c_str());

    self->f = fuse_new(&args, &xmp_oper, sizeof(xmp_oper), self->client);
    if (!self->f)
    {
        qDebug() << "[Start] fuse_new failed";
        fuse_opt_free_args(&args);
        return;
    }

    struct fuse_session *se = fuse_get_session(self->f);

    if (fuse_mount(self->f, self->mountpoint) != 0)
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
    qDebug() << "joined";
}

#endif
