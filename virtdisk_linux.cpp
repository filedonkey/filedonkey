#ifdef __linux__

// Example source:
// https://github.com/libfuse/libfuse/blob/master/example/passthrough.c

#include "virtdisk.h"
#include "fuseclient.h"

#include <thread>

#define FUSE_USE_VERSION 31

#define _GNU_SOURCE

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif

#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#ifdef __FreeBSD__
#include <sys/socket.h>
#include <sys/un.h>
#endif
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

static int fill_dir_plus = 0;

static std::thread thread;
static struct fuse *f;
static struct fuse_chan *ch;
static char *mountpoint = "/home/vboxuser/Windows PC";

static FUSEClient *g_Client;

VirtDisk::VirtDisk(const Connection& conn) : conn(conn)
{
}

VirtDisk::~VirtDisk()
{
    fuse_exit(f);
    fuse_unmount(f);
}

static int mknod_wrapper(int dirfd, const char *path, const char *link,
                         int mode, dev_t rdev)
{
    int res;

    if (S_ISREG(mode)) {
        res = openat(dirfd, path, O_CREAT | O_EXCL | O_WRONLY, mode);
        if (res >= 0)
            res = close(res);
    } else if (S_ISDIR(mode)) {
        res = mkdirat(dirfd, path, mode);
    } else if (S_ISLNK(mode) && link != NULL) {
        res = symlinkat(link, dirfd, path);
    } else if (S_ISFIFO(mode)) {
        res = mkfifoat(dirfd, path, mode);
#ifdef __FreeBSD__
    } else if (S_ISSOCK(mode)) {
        struct sockaddr_un su;
        int fd;

        if (strlen(path) >= sizeof(su.sun_path)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd >= 0) {
            /*
             * We must bind the socket to the underlying file
             * system to create the socket file, even though
             * we'll never listen on this socket.
             */
            su.sun_family = AF_UNIX;
            strncpy(su.sun_path, path, sizeof(su.sun_path));
            res = bindat(dirfd, fd, (struct sockaddr*)&su,
                         sizeof(su));
            if (res == 0)
                close(fd);
        } else {
            res = -1;
        }
#endif
    } else {
        res = mknodat(dirfd, path, mode, rdev);
    }

    return res;
}

static void *xmp_init(struct fuse_conn_info *conn,
                      struct fuse_config *cfg)
{
    (void) conn;
    cfg->use_ino = 1;

    /* parallel_direct_writes feature depends on direct_io features.
       To make parallel_direct_writes valid, need either set cfg->direct_io
       in current function (recommended in high level API) or set fi->direct_io
       in xmp_create() or xmp_open(). */
     cfg->direct_io = 1;
//    cfg->parallel_direct_writes = 1;

    /* Pick up changes from lower filesystem right away. This is
       also necessary for better hardlink support. When the kernel
       calls the unlink() handler, it does not know the inode of
       the to-be-removed entry and can therefore not invalidate
       the cache of the associated inode - resulting in an
       incorrect st_nlink value being reported for any remaining
       hardlinks to this inode. */
    if (!cfg->auto_cache) {
        cfg->entry_timeout = 0;
        cfg->attr_timeout = 0;
        cfg->negative_timeout = 0;
    }

    return NULL;
}

static int xmp_getattr(const char *path, struct stat *stbuf,
                       struct fuse_file_info *fi)
{
    qDebug() << "[xmp_getattr] path: " << path;

    (void) fi;

    //------------------------------------------------------------------------------------
    // Network tests
    //------------------------------------------------------------------------------------
    struct fuse_context *context = fuse_get_context();
    qDebug() << "[xmp_read] context:" << context << context->private_data;
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
    }

    return result->status;

    //------------------------------------------------------------------------------------

    // (void) fi;
    // int res;

    // res = lstat(path, stbuf);
    // if (res == -1)
    //     return -errno;

    // return 0;
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

    // int res;

    // res = readlink(path, buf, size - 1);
    // if (res == -1)
    //     return -errno;

    // buf[res] = '\0';
    // return 0;
}


static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi,
                       enum fuse_readdir_flags flags)
{
    qDebug() << "[xmp_readdir] path: " << path;

    (void) offset;
    (void) fi;
    (void) flags;

    //------------------------------------------------------------------------------------
    // Network tests
    //------------------------------------------------------------------------------------
    struct fuse_context *context = fuse_get_context();
    qDebug() << "[xmp_readdir] context:" << context << context->private_data;
    FUSEClient *client = g_Client; // (FUSEClient*)context->private_data;

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

        filler(buf, fd->name, &st, /*nextoff*/0, fuse_fill_dir_flags::FUSE_FILL_DIR_PLUS);
    }
    qDebug() << "after for";

    int status = result->status;

    return status;
    //------------------------------------------------------------------------------------

    DIR *dp;
    struct dirent *de;

    dp = opendir(path);
    if (dp == NULL)
        return -errno;

    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        if (fill_dir_plus) {
            fstatat(dirfd(dp), de->d_name, &st,
                    AT_SYMLINK_NOFOLLOW);
        } else {
            memset(&st, 0, sizeof(st));
            st.st_ino = de->d_ino;
            st.st_mode = de->d_type << 12;
        }
        if (filler(buf, de->d_name, &st, 0, fuse_fill_dir_flags::FUSE_FILL_DIR_PLUS)) // NOTE: Last argument was fill_dir_plus and not FUSE_FILL_DIR_PLUS
            break;
    }

    closedir(dp);
    return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
    qDebug() << "[xmp_mknod] path: " << path;

    int res;

    res = mknod_wrapper(AT_FDCWD, path, NULL, mode, rdev);
    if (res == -1)
        return -errno;

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
    qDebug() << "[xmp_symlink] path: " << from << to;

    int res;

    res = symlink(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_rename(const char *from, const char *to, unsigned int flags)
{
    qDebug() << "[xmp_rename] path: " << from << to;

    int res;

    if (flags)
        return -EINVAL;

    res = rename(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_link(const char *from, const char *to)
{
    qDebug() << "[xmp_link] path: " << from << to;

    int res;

    res = link(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_chmod(const char *path, mode_t mode,
                     struct fuse_file_info *fi)
{
    qDebug() << "[xmp_chmod] path: " << path;

    (void) fi;
    int res;

    res = chmod(path, mode);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid,
                     struct fuse_file_info *fi)
{
    qDebug() << "[xmp_chown] path: " << path;

    (void) fi;
    int res;

    res = lchown(path, uid, gid);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_truncate(const char *path, off_t size,
                        struct fuse_file_info *fi)
{
    qDebug() << "[xmp_truncate] path: " << path;

    int res;

    if (fi != NULL)
        res = ftruncate(fi->fh, size);
    else
        res = truncate(path, size);
    if (res == -1)
        return -errno;

    return 0;
}

#ifdef HAVE_UTIMENSAT
static int xmp_utimens(const char *path, const struct timespec ts[2],
                       struct fuse_file_info *fi)
{
    qDebug() << "[xmp_utimens] path: " << path;

    (void) fi;
    int res;

    /* don't use utime/utimes since they follow symlinks */
    res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
    if (res == -1)
        return -errno;

    return 0;
}
#endif

static int xmp_create(const char *path, mode_t mode,
                      struct fuse_file_info *fi)
{
    qDebug() << "[xmp_create] path: " << path;

    int res;

    res = open(path, fi->flags, mode);
    if (res == -1)
        return -errno;

    fi->fh = res;
    return 0;
}

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_open] path: " << path;

    int res;

    res = open(path, fi->flags);
    if (res == -1)
        return -errno;

    /* Enable direct_io when open has flags O_DIRECT to enjoy the feature
        parallel_direct_writes (i.e., to get a shared lock, not exclusive lock,
        for writes to the same file). */
    if (fi->flags & O_DIRECT) {
        fi->direct_io = 1;
//        fi->parallel_direct_writes = 1;
    }

    fi->fh = res;
    return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
    qDebug() << "[xmp_read] path: " << path;

    (void) fi;

    //------------------------------------------------------------------------------------
    // Network tests
    //------------------------------------------------------------------------------------
    struct fuse_context *context = fuse_get_context();
    qDebug() << "[xmp_read] context:" << context << context->private_data;
    FUSEClient *client = g_Client; // (FUSEClient*)context->private_data;

    Ref<ReadResult> result = client->FD_read(path, size, offset);

    if (result->status == 0)
    {
        memcpy(buf, result->data, result->size);
    }

    return result->status;
    //------------------------------------------------------------------------------------

    int fd;
    int res;

    if(fi == NULL)
        fd = open(path, O_RDONLY);
    else
        fd = fi->fh;

    if (fd == -1)
        return -errno;

    res = pread(fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    if(fi == NULL)
        close(fd);
    return res;
}

static int xmp_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_write] path: " << path;

    int fd;
    int res;

    (void) fi;
    if(fi == NULL)
        fd = open(path, O_WRONLY);
    else
        fd = fi->fh;

    if (fd == -1)
        return -errno;

    res = pwrite(fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    if(fi == NULL)
        close(fd);
    return res;
}

static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
    qDebug() << "[xmp_statfs] path: " << path;

    //------------------------------------------------------------------------------------
    // Network tests
    //------------------------------------------------------------------------------------
    struct fuse_context *context = fuse_get_context();
    qDebug() << "[xmp_read] context:" << context << context->private_data;
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

    // int res;

    // res = statvfs(path, stbuf);
    // if (res == -1)
    //     return -errno;

    // return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_release] path: " << path;

    (void) path;
    close(fi->fh);
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
    qDebug() << "[xmp_fallocate] path: " << path;

    int fd;
    int res;

    (void) fi;

    if (mode)
        return -EOPNOTSUPP;

    if(fi == NULL)
        fd = open(path, O_WRONLY);
    else
        fd = fi->fh;

    if (fd == -1)
        return -errno;

    res = -posix_fallocate(fd, offset, length);

    if(fi == NULL)
        close(fd);
    return res;
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int xmp_setxattr(const char *path, const char *name, const char *value,
                        size_t size, int flags)
{
    qDebug() << "[xmp_setxattr] path: " << path;

    int res = lsetxattr(path, name, value, size, flags);
    if (res == -1)
        return -errno;
    return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value,
                        size_t size)
{
    qDebug() << "[xmp_getxattr] path: " << path;

    int res = lgetxattr(path, name, value, size);
    if (res == -1)
        return -errno;
    return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
    qDebug() << "[xmp_listxattr] path: " << path;

    int res = llistxattr(path, list, size);
    if (res == -1)
        return -errno;
    return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
    qDebug() << "[xmp_removexattr] path: " << path;

    int res = lremovexattr(path, name);
    if (res == -1)
        return -errno;
    return 0;
}
#endif /* HAVE_SETXATTR */

#ifdef HAVE_COPY_FILE_RANGE
static ssize_t xmp_copy_file_range(const char *path_in,
                                   struct fuse_file_info *fi_in,
                                   off_t offset_in, const char *path_out,
                                   struct fuse_file_info *fi_out,
                                   off_t offset_out, size_t len, int flags)
{
    qDebug() << "[xmp_copy_file_range] path: " << path_in;

    int fd_in, fd_out;
    ssize_t res;

    if(fi_in == NULL)
        fd_in = open(path_in, O_RDONLY);
    else
        fd_in = fi_in->fh;

    if (fd_in == -1)
        return -errno;

    if(fi_out == NULL)
        fd_out = open(path_out, O_WRONLY);
    else
        fd_out = fi_out->fh;

    if (fd_out == -1) {
        close(fd_in);
        return -errno;
    }

    res = copy_file_range(fd_in, &offset_in, fd_out, &offset_out, len,
                          flags);
    if (res == -1)
        res = -errno;

    if (fi_out == NULL)
        close(fd_out);
    if (fi_in == NULL)
        close(fd_in);

    return res;
}
#endif

static off_t xmp_lseek(const char *path, off_t off, int whence, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_lseek] path: " << path;

    int fd;
    off_t res;

    if (fi == NULL)
        fd = open(path, O_RDONLY);
    else
        fd = fi->fh;

    if (fd == -1)
        return -errno;

    res = lseek(fd, off, whence);
    if (res == -1)
        res = -errno;

    if (fi == NULL)
        close(fd);
    return res;
}

static const struct fuse_operations xmp_oper = {
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
    .open		= xmp_open,
    .read		= xmp_read,
    .write		= xmp_write,
    .statfs		= xmp_statfs,
    .release	= xmp_release,
    .fsync		= xmp_fsync,
#ifdef HAVE_SETXATTR
    .setxattr	= xmp_setxattr,
    .getxattr	= xmp_getxattr,
    .listxattr	= xmp_listxattr,
    .removexattr	= xmp_removexattr,
#endif
    .readdir	= xmp_readdir,
    .init       = xmp_init,
    .access		= xmp_access,
#ifdef HAVE_UTIMENSAT
    .utimens	= xmp_utimens,
#endif
    .create 	= xmp_create,
#ifdef HAVE_POSIX_FALLOCATE
    .fallocate	= xmp_fallocate,
#endif
#ifdef HAVE_COPY_FILE_RANGE
    .copy_file_range = xmp_copy_file_range,
#endif
    .lseek		= xmp_lseek,
};

static void Start(Connection *conn)
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

    g_Client = new FUSEClient(conn);

    int argc = 3;
    char *argv[] = {"FileDonkey", "-o", "fsname=Windows PC"};// "/var/tmp/fuse"};
    system("mkdir -p /var/tmp/fuse");
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    int err = -1;

            f = fuse_new(&args, &xmp_oper, sizeof(xmp_oper), conn);
            fuse_mount(f, mountpoint);
            qDebug() << "before fuse_set_signal_handlers call";
            if (fuse_set_signal_handlers(fuse_get_session(f)) != 0) {
                fprintf(stderr, "Failed to set up signal handlers\n");
                perror("fuse_set_signal_handlers");
                fuse_destroy(f);
                fuse_unmount(f);
                return;
            }
            qDebug() << "before fuse_loop call";
            fuse_loop(f);
            qDebug() << "after fuse_loop call";
            fuse_remove_signal_handlers(fuse_get_session(f));
            fuse_unmount(f);
            fuse_destroy(f);

    // int ret = fuse_main_real(args.argc, args.argv, &xmp_oper,
    //                          sizeof(xmp_oper), (void *)conn);

//    umask(0);
//        loopback.blocksize = 4096;
//        loopback.case_insensitive = 0;
//        if (fuse_opt_parse(&args, &loopback, loopback_opts, NULL) == -1) {
//            exit(1);
//        }
//    qDebug() << "before fuse_main call";
//     int res = fuse_main(argc, argv, &xmp_oper, conn);
//     qDebug() << "fuse_main result: " << res;
//     fuse_opt_free_args(&args);
}

void VirtDisk::mount(const QString &mountPoint)
{
    int argc = 4;
    char *argv[] = {"FileDonkey", "/Users/Guest/Public/fuse/", "-o", "volname=Windows PC"};
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

//    loopback.blocksize = 4096;
//    loopback.case_insensitive = 0;
//    if (fuse_opt_parse(&args, &loopback, loopback_opts, NULL) == -1) {
//        exit(1);
//    }

//    umask(0);
//    int res = fuse_main(args.argc, args.argv, &loopback_oper, NULL);

//    qDebug() << "fuse_main result: " << res;

    thread = std::thread(Start, &conn);
}

//int main(int argc, char *argv[])
//{
//    enum { MAX_ARGS = 10 };
//    int i,new_argc;
//    char *new_argv[MAX_ARGS];

//    umask(0);
//    /* Process the "--plus" option apart */
//    for (i=0, new_argc=0; (i<argc) && (new_argc<MAX_ARGS); i++) {
//        if (!strcmp(argv[i], "--plus")) {
//            fill_dir_plus = FUSE_FILL_DIR_PLUS;
//        } else {
//            new_argv[new_argc++] = argv[i];
//        }
//    }
//    return fuse_main(new_argc, new_argv, &xmp_oper, NULL);
//}

#endif
