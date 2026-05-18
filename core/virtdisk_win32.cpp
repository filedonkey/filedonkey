#if defined(_WIN32)

#include "virtdisk.h"
#include "fuseclient.h"
#include "fusebackend_types.h"

#include <cassert>
#include <thread>
#include <iostream>
#include <fileapi.h>

#define FUSE_USE_VERSION 30

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(_WIN32)
#include "statvfs_win32.h"
#include "lstat_win32.h"
#include "pread_win32.h"
#endif

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif

#include <fuse/fuse.h>
#include <fuse/winfsp_fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif
#include <direct.h>

#define mkdir(path, mode)    _mkdir(path)

static std::thread thread;
static struct fuse *f;
static struct fuse_chan *ch;
static struct fuse_session *se;
static char *mountpoint = "D:\\";

static FUSEClient *g_Client;

VirtDisk::VirtDisk(const Connection& conn) : conn(conn), client(new FUSEClient(&this->conn))
{
}

VirtDisk::~VirtDisk()
{
    qDebug() << "~VirtDisk";
    fuse_exit(f);
    fuse_remove_signal_handlers(se);
    fuse_unmount(mountpoint, ch);
}

static time_t filetimeToUnixTime(const FILETIME *ft) {
    // if (!is_filetime_set(ft))
    //     return 0;

    ULONGLONG ll = (ULONGLONG(ft->dwHighDateTime) << 32) + ft->dwLowDateTime;
    return time_t((ll - 116444736000000000LL) / 10000000LL);
}

static int xmp_getattr(const char *path, struct fuse_stat /*stat*/ *stbuf)
{
    qDebug() << "[xmp_getattr] path: " << path;

    //------------------------------------------------------------------------------------
    // Network tests
    //------------------------------------------------------------------------------------
    struct fuse_context *context = fuse_get_context();
    qDebug() << "[xmp_getattr] context:" << context << context->private_data;
    FUSEClient *client = g_Client; // (FUSEClient*)context->private_data;

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

        if (QString(path) == QString("/")) {
            stbuf->st_mode = 16877;
        }

        qDebug() << "\tst_atimespec" << stbuf->st_atim.tv_sec << stbuf->st_atim.tv_nsec;
        qDebug() << "\tst_birthtimespec" << stbuf->st_birthtim.tv_sec << stbuf->st_birthtim.tv_nsec;
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

    //------------------------------------------------------------------------------------


}

static int xmp_access(const char *path, int mask)
{
    qDebug() << "[xmp_access] path: " << path;



    return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
    qDebug() << "[xmp_readlink] path: " << path;


    //------------------------------------------------------------------------------------
    // Network tests
    //------------------------------------------------------------------------------------
    struct fuse_context *context = fuse_get_context();
    qDebug() << "[xmp_readlink] context:" << context << context->private_data;
    FUSEClient *client = g_Client; // (FUSEClient*)context->private_data;

    Ref<ReadlinkResult> result = client->FD_readlink(path, size);

    if (result->status == 0)
    {
        memcpy(buf, result->data, result->size);
    }

    return result->status;

    //------------------------------------------------------------------------------------

}


static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_readdir] path: " << path;

    (void) offset;
    (void) fi;

    //------------------------------------------------------------------------------------
    // Network tests
    //------------------------------------------------------------------------------------
    struct fuse_context *context = fuse_get_context();
    qDebug() << "[xmp_readdir] context:" << context << context->private_data;
    FUSEClient *client = g_Client; // (FUSEClient*)context->private_data;

    Ref<ReaddirResult> result = client->FD_readdir(path);

    struct fuse_stat st;
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
        st.st_size = 146;
        st.st_blksize = 4096;
        st.st_blocks = 2;
        st.st_atim.tv_sec = 1763752599;
        st.st_atim.tv_nsec = 302761200;
        st.st_mtim.tv_sec = 1747514473;
        st.st_mtim.tv_nsec = 21076073;
        st.st_ctim.tv_sec = 1747514473;
        st.st_ctim.tv_nsec = 21076073;

        filler(buf, fd->name, &st, /*nextoff*/0);
    }
    qDebug() << "after for";

    int status = result->status;

    return status;
    //------------------------------------------------------------------------------------


}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
    qDebug() << "[xmp_mknod] path: " << path;

    int res;

    /* On Linux this could just be 'mknod(path, mode, rdev)' but this
       is more portable */
    // if (S_ISREG(mode)) {
    //     res = open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
    //     if (res >= 0)
    //         res = close(res);
    // } else if (S_ISFIFO(mode))
    //     res = mkfifo(path, mode);
    // else
    //     res = mknod(path, mode, rdev);
    // if (res == -1)
    //     return -errno;

    return 0;
}

static int xmp_mkdir(const char *path, fuse_mode_t mode)
{
    qDebug() << "[xmp_mkdir] path: " << path;

    int res;

    res = mkdir(path, mode);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_unlink(const char *path)
{
    qDebug() << "[xmp_unlink] path: " << path;

    int res;

    res = unlink(path);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_rmdir(const char *path)
{
    qDebug() << "[xmp_rmdir] path: " << path;

    int res;

    res = rmdir(path);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_symlink(const char *from, const char *to)
{
    qDebug() << "[xmp_symlink] from: " << from;

    int res;



    return 0;
}

static int xmp_rename(const char *from, const char *to)
{
    qDebug() << "[xmp_rename] from: " << from;

    int res;

    res = rename(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_link(const char *from, const char *to)
{
    qDebug() << "[xmp_link] path: " << from;

    int res;


    return 0;
}

static int xmp_chmod(const char *path, mode_t mode)
{
    qDebug() << "[xmp_chmod] path: " << path;

    int res;

    res = chmod(path, mode);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_chown(const char *path, fuse_uid_t uid, fuse_gid_t gid)
{
    qDebug() << "[xmp_chown] path: " << path;

    int res;



    return 0;
}

static int xmp_truncate(const char *path, off_t size)
{
    qDebug() << "[xmp_truncate] path: " << path;

    int res;

    res = truncate(path, size);
    if (res == -1)
        return -errno;

    return 0;
}

#ifdef HAVE_UTIMENSAT
static int xmp_utimens(const char *path, const struct timespec ts[2])
{
    int res;

    /* don't use utime/utimes since they follow symlinks */
    res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
    if (res == -1)
        return -errno;

    return 0;
}
#endif

static int xmp_create(const char *path, fuse_mode_t mode, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_create] path: " << path;

    struct fuse_context *context = fuse_get_context();
    FUSEClient *client = (FUSEClient *)context->private_data;
    assert(client && "[xmp_create] FUSEClient not found");

    Ref<CreateResult> result = client->FD_create(path, mode, fi->flags);

    qDebug() << "[xmp_create] status: " << result->status;

    return result->status;
}

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_open] path: " << path;

    return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi) {
    (void)fi;

    struct fuse_context *context = fuse_get_context();
    FUSEClient *client = (FUSEClient *)context->private_data;
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
    qDebug() << "[xmp_write] path: " << path;

    (void) fi;

    //------------------------------------------------------------------------------------
    // Network tests
    //------------------------------------------------------------------------------------
    struct fuse_context *context = fuse_get_context();
    FUSEClient *client = (FUSEClient *)context->private_data;
    assert(client && "[xmp_write] FUSEClient not found");

    Ref<WriteResult> result = client->FD_write(path, buf, size, offset);

    qDebug() << "[xmp_write] incoming result status:" << result->status;

    return result->status;
    //------------------------------------------------------------------------------------


    return 0;
}

static int xmp_statfs(const char *path, struct fuse_statvfs *stbuf)
{
    qDebug() << "[xmp_statfs] path: " << path;

    //------------------------------------------------------------------------------------
    // Network tests
    //------------------------------------------------------------------------------------
    struct fuse_context *context = fuse_get_context();
    qDebug() << "[xmp_statfs] context:" << context << context->private_data;
    FUSEClient *client = g_Client; // (FUSEClient*)context->private_data;

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

    //------------------------------------------------------------------------------------


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

#ifdef HAVE_POSIX_FALLOCATE
static int xmp_fallocate(const char *path, int mode,
                         off_t offset, off_t length, struct fuse_file_info *fi)
{
    int fd;
    int res;

    (void) fi;

    if (mode)
        return -EOPNOTSUPP;

    fd = open(path, O_WRONLY);
    if (fd == -1)
        return -errno;

    res = -posix_fallocate(fd, offset, length);

    close(fd);
    return res;
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int xmp_setxattr(const char *path, const char *name, const char *value,
                        size_t size, int flags)
{
    int res = lsetxattr(path, name, value, size, flags);
    if (res == -1)
        return -errno;
    return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value,
                        size_t size)
{
    int res = lgetxattr(path, name, value, size);
    if (res == -1)
        return -errno;
    return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
    int res = llistxattr(path, list, size);
    if (res == -1)
        return -errno;
    return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
    int res = lremovexattr(path, name);
    if (res == -1)
        return -errno;
    return 0;
}
#endif /* HAVE_SETXATTR */

/*
 * List of fuse operations that are the same for macFUSE and Dokan FUSE
 *
    getattr
    readlink
    getdir
    mknod
    mkdir
    unlink
    rmdir
    symlink
    rename
    link
    chmod
    chown
    truncate
  ? utime
    open
    read
    write
    statfs
  - flush
    release
    fsync
  ? setxattr
  ? getxattr
  ? listxattr
  ? removexattr
  - opendir
    readdir
  - releasedir
  - fsyncdir
    access
  - create
  - ftruncate
  - fgetattr
  - lock
  - utimens
  - bmap
 */

// WinFsp FUSE: https://github.com/winfsp/winfsp/blob/master/tst/winfsp-tests/fuse-test.c

static struct fuse_operations xmp_oper = {
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
#ifdef HAVE_UTIMENSAT
    .utimens	= xmp_utimens,
#endif
    .readdir	= xmp_readdir,
    .access		= xmp_access,
    .create		= xmp_create,
};

// What was used during navigation:
// + xmp_getattr
// + xmp_open
// + xmp_release // This method is optional and can safely be left unimplemented
// + xmp_read
// + xmp_readdir
// + xmp_statfs

static void Start(VirtDisk *self, Connection *conn)
{
    conn->socket = new QTcpSocket();
    qDebug() << "[Start] try to connect";
    conn->socket->connectToHost(QHostAddress(conn->machineAddress), conn->machinePort);
    if (!conn->socket->waitForConnected())
    {
        qDebug()
        << "[Start] socket connection error: "
        << conn->socket->errorString();
        return;
    }

    qDebug() << "[Start] socket connected";

    g_Client = self->client;

    int argc = 4;
    char *argv[] = {"FileDonkey", "M:", "-o", "volname=MacBook Pro"};
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    int err = -1;

    if (fuse_parse_cmdline(&args, &mountpoint, NULL, NULL) != -1 &&
        (ch = fuse_mount(mountpoint, &args)) != NULL) {
        f = fuse_new(ch, &args, &xmp_oper,
                     sizeof(xmp_oper), self->client);


        se = fuse_get_session(f);
        fuse_set_signal_handlers(se);


        qDebug() << "before fuse_loop call";
        fuse_loop(f);
        qDebug() << "after fuse_loop call";



        fuse_exit(f);
        fuse_unmount(mountpoint, ch);
    }

    fuse_opt_free_args(&args);
}

void VirtDisk::mount(const QString &mountPoint)
{


    thread = std::thread(Start, this, &conn);
}



#endif