#define __FUSE__

#if defined(__WIN32) && defined(__FUSE__)

#include "virtdisk.h"
#include <thread>

#define FUSE_USE_VERSION 30

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

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

VirtDisk::VirtDisk(const Connection& conn) : conn(conn)
{
}

VirtDisk::~VirtDisk()
{
    qDebug() << "~VirtDisk";
    fuse_exit(f);
    fuse_remove_signal_handlers(se);
    fuse_unmount(mountpoint, ch);
}

static int xmp_getattr(const char *path, struct FUSE_STAT /*stat*/ *stbuf)
{
    qDebug() << "[xmp_getattr] path: " << path;

    // int res;

    // res = lstat(path, stbuf);
    // if (res == -1)
    //     return -errno;

    return 0;
}

static int xmp_access(const char *path, int mask)
{
    qDebug() << "[xmp_access] path: " << path;

    int res;

    res = access(path, mask);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
    qDebug() << "[xmp_readlink] path: " << path;

    // int res;

    // res = readlink(path, buf, size - 1);
    // if (res == -1)
    //     return -errno;

    // buf[res] = '\0';
    return 0;
}


static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_readdir] path: " << path;

    DIR *dp;
    struct dirent *de;

    (void) offset;
    (void) fi;

    dp = opendir(path);
    if (dp == NULL)
        return -errno;

    // while ((de = readdir(dp)) != NULL) {
    //     struct stat st;
    //     memset(&st, 0, sizeof(st));
    //     st.st_ino = de->d_ino;
    //     st.st_mode = de->d_type << 12;
    //     if (filler(buf, de->d_name, &st, 0))
    //         break;
    // }

    closedir(dp);
    return 0;
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

static int xmp_mkdir(const char *path, mode_t mode)
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

    // res = symlink(from, to);
    // if (res == -1)
    //     return -errno;

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

    // res = link(from, to);
    // if (res == -1)
    //     return -errno;

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

static int xmp_chown(const char *path, uid_t uid, gid_t gid)
{
    qDebug() << "[xmp_chown] path: " << path;

    int res;

    // res = lchown(path, uid, gid);
    // if (res == -1)
    //     return -errno;

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

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_open] path: " << path;

    int res;

    res = open(path, fi->flags);
    if (res == -1)
        return -errno;

    close(res);
    return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
    qDebug() << "[xmp_read] path: " << path;

    // int fd;
    // int res;

    // (void) fi;
    // fd = open(path, O_RDONLY);
    // if (fd == -1)
    //     return -errno;

    // res = pread(fd, buf, size, offset);
    // if (res == -1)
    //     res = -errno;

    // close(fd);
    // return res;
    return 0;
}

static int xmp_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_write] path: " << path;

    // int fd;
    // int res;

    // (void) fi;
    // fd = open(path, O_WRONLY);
    // if (fd == -1)
    //     return -errno;

    // res = pwrite(fd, buf, size, offset);
    // if (res == -1)
    //     res = -errno;

    // close(fd);
    // return res;
    return 0;
}

static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
    // int res;

    // res = statvfs(path, stbuf);
    // if (res == -1)
    //     return -errno;

    qDebug() << "[xmp_statfs] path: " << path;

    stbuf->f_bavail = 12006580;
    stbuf->f_bfree = 12006580;
    stbuf->f_blocks = 61202533;
    stbuf->f_bsize = 1048576;
    stbuf->f_ffree = 480263200;
    stbuf->f_files = 480765579;
    stbuf->f_favail = 480263200;
    stbuf->f_flag = 1;
    stbuf->f_frsize = 4096;
    stbuf->f_fsid = 16777226;
    stbuf->f_namemax = 255;

    return 0;
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

static struct fuse_operations xmp_oper = {
    .getattr	= xmp_getattr,
    .readlink	= xmp_readlink,
    .mknod		= xmp_mknod,
    .mkdir		= xmp_mkdir,
    .unlink		= xmp_unlink,
    .rmdir		= xmp_rmdir,
    .symlink	= xmp_symlink,
    .rename		= xmp_rename,
    .link		= xmp_link,
    .chmod		= xmp_chmod,
    .chown		= xmp_chown,
    .truncate	= xmp_truncate,
#ifdef HAVE_UTIMENSAT
    .utimens	= xmp_utimens,
#endif
    .open		= xmp_open,
    .read		= xmp_read,
    .write		= xmp_write,
    .statfs		= xmp_statfs,
    .release	= xmp_release,
    .fsync		= xmp_fsync,
    .readdir	= xmp_readdir,
#ifdef HAVE_POSIX_FALLOCATE
    .fallocate	= xmp_fallocate,
#endif
#ifdef HAVE_SETXATTR
    .setxattr	= xmp_setxattr,
    .getxattr	= xmp_getxattr,
    .listxattr	= xmp_listxattr,
    .removexattr	= xmp_removexattr,
#endif
    .access		= xmp_access,
};

static void Start(const Connection &conn)
{
    int argc = 4;
    char *argv[] = {"FileDonkey", "M:", "-o", "volname=MacBook Pro"};
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    int err = -1;

    if (fuse_parse_cmdline(&args, &mountpoint, NULL, NULL) != -1 &&
        (ch = fuse_mount(mountpoint, &args)) != NULL) {

        f = fuse_new(ch, &args, &xmp_oper,
                     sizeof(xmp_oper), (void *)&conn);


        se = fuse_get_session(f);
        fuse_set_signal_handlers(se);


        qDebug() << "before fuse_loop call";
        fuse_loop(f);
        qDebug() << "after fuse_loop call";


        //            if (se != NULL) {
        //                if (fuse_set_signal_handlers(se) != -1) {
        //                    fuse_session_add_chan(se, ch);
        //                    err = fuse_session_loop(se);
        //                    fuse_remove_signal_handlers(se);
        //                    fuse_session_remove_chan(ch);
        //                }
        //                fuse_session_destroy(se);
        //            }
        fuse_exit(f);
        fuse_unmount(mountpoint, ch);
    }

    fuse_opt_free_args(&args);
}

void VirtDisk::mount(const QString &mountPoint)
{
    // int argc = 4;
    // char *argv[] = {"FileDonkey", "M:", "-o", "volname=MacBook Pro"};
    // struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    // loopback.blocksize = 4096;
    // loopback.case_insensitive = 0;
    // if (fuse_opt_parse(&args, &loopback, loopback_opts, NULL) == -1) {
    //     exit(1);
    // }

    // umask(0);
    // int res = fuse_main(args.argc, args.argv, &xmp_oper, NULL);

    // qDebug() << "fuse_main result: " << res;

    // fuse_opt_free_args(&args);

    thread = std::thread(Start, conn);
}

// int main(int argc, char *argv[])
// {
//     umask(0);
//     return fuse_main(argc, argv, &xmp_oper, NULL);
// }

#endif
