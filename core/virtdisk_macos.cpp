#ifdef __APPLE__

// Example source:
// https://github.com/macfuse/demo/blob/master/LoopbackFS-C/loopback/loopback.c

// https://github.com/macos-fuse-t/libfuse/blob/master/example/fusexmp_fh.c

// Docan FUSE operations mapping
// https://github.com/dokan-dev/dokany/wiki/FUSE

#include "virtdisk.h"
#include "fuseclient.h"


#include <QDebug>
#include <QHostAddress>
#include <cassert>
#include <thread>

#define FUSE_USE_VERSION 31

#define HAVE_SETXATTR	1

#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#else

#endif

#include <fuse.h>
#ifndef __APPLE__
#include <ulockmgr.h>
#endif
#include <stdio.h>
#include <stdlib.h>
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
#ifndef __APPLE__
#include <sys/file.h> /* flock(2) */
#endif

#include <sys/param.h>

#ifdef __APPLE__

#include <fcntl.h>
#include <sys/vnode.h>

#if defined(_POSIX_C_SOURCE)
typedef unsigned char  u_char;
typedef unsigned short u_short;
typedef unsigned int   u_int;
typedef unsigned long  u_long;
#endif

#include <sys/attr.h>

#define G_PREFIX			"org"
#define G_KAUTH_FILESEC_XATTR G_PREFIX 	".apple.system.Security"
#define A_PREFIX			"com"
#define A_KAUTH_FILESEC_XATTR A_PREFIX 	".apple.system.Security"
#define XATTR_APPLE_PREFIX		"com.apple."

#endif /* __APPLE__ */

static std::thread thread;
static struct fuse *f;
static struct fuse_chan *ch;
static char *mountpoint = "/Users/igorgoremykin";

static FUSEClient *g_Client;


VirtDisk::VirtDisk(const Connection& conn) : conn(conn), client(new FUSEClient(&this->conn))
{
}

VirtDisk::~VirtDisk()
{
    fuse_exit(f);
    fuse_unmount(mountpoint, ch);
}

static int xmp_getattr(const char *path, struct stat *stbuf)
{
    qDebug() << "[xmp_getattr] path: " << path;

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
        stbuf->st_atimespec.tv_sec = result->st_atim.tv_sec;
        stbuf->st_atimespec.tv_nsec = result->st_atim.tv_nsec;
        stbuf->st_mtimespec.tv_sec = result->st_mtim.tv_sec;
        stbuf->st_mtimespec.tv_nsec = result->st_mtim.tv_nsec;
        stbuf->st_ctimespec.tv_sec = result->st_ctim.tv_sec;
        stbuf->st_ctimespec.tv_nsec = result->st_ctim.tv_nsec;
    }

    return result->status;

    //------------------------------------------------------------------------------------


}

static int xmp_fgetattr(const char *path, struct stat *stbuf,
            struct fuse_file_info *fi)
{
    qDebug() << "[xmp_fgetattr] path: " << path;

    int res;

    (void) path;

    res = fstat(fi->fh, stbuf);
    if (res == -1)
        return -errno;

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

struct xmp_dirp {
    DIR *dp;
    struct dirent *entry;
    off_t offset;
};

static int xmp_opendir(const char *path, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_opendir] path: " << path;

    int res;
    struct xmp_dirp *d = (struct xmp_dirp *)malloc(sizeof(struct xmp_dirp));
    if (d == NULL)
        return -ENOMEM;

    d->dp = opendir(path);
    if (d->dp == NULL) {
        res = -errno;
        free(d);
        return res;
    }
    d->offset = 0;
    d->entry = NULL;

    fi->fh = (unsigned long) d;
    return 0;
}

static inline struct xmp_dirp *get_dirp(struct fuse_file_info *fi)
{
    return (struct xmp_dirp *) (uintptr_t) fi->fh;
}

static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
               off_t offset, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_readdir] path: " << path;

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
    for (int i = 0; i < result->count; ++i)
    {
        qDebug() << "for i" << i;
        FindData *fd = (FindData *)result->findData + i;
        qDebug() << "[xmp_readdir] incoming findData name:" << fd->name;
        qDebug() << "[xmp_readdir] incoming findData st_ino:" << fd->st_ino;
        qDebug() << "[xmp_readdir] incoming findData st_mode:" << fd->st_mode;

         st.st_ino = fd->st_ino;
         st.st_mode = fd->st_mode;

         filler(buf, fd->name, &st, /*nextoff*/0);
    }
    qDebug() << "after for";

    return result->status;
    //------------------------------------------------------------------------------------

}

static int xmp_releasedir(const char *path, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_releasedir] path: " << path;

    struct xmp_dirp *d = get_dirp(fi);
    (void) path;
    closedir(d->dp);
    free(d);
    return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
    qDebug() << "[xmp_mknod] path: " << path;

    int res;

    if (S_ISFIFO(mode))
        res = mkfifo(path, mode);
    else
        res = mknod(path, mode, rdev);
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

    struct fuse_context *context = fuse_get_context();
    FUSEClient *client = (FUSEClient *)context->private_data;
    assert(client && "[xmp_unlink] FUSEClient not found");

    Ref<UnlinkResult> result = client->FD_unlink(path);

    return result->status;

//    int res;

//    res = unlink(path);
//    if (res == -1)
//        return -errno;

//    return 0;
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
    qDebug() << "[xmp_symlink] path: " << from;

    int res;

    res = symlink(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_rename(const char *from, const char *to)
{
    qDebug() << "[xmp_rename] path: " << from;

    int res;
    res = rename(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

#ifdef __APPLE__

static int xmp_setvolname(const char *volname)
{
    qDebug() << "[xmp_setvolname] path: " << volname;

    (void)volname;
    return 0;
}

static int xmp_exchange(const char *path1, const char *path2,
            unsigned long options)
{
    qDebug() << "[xmp_exchange] path: " << path1 << path2;

    int res;

    res = exchangedata(path1, path2, options);
    if (res == -1)
        return -errno;

    return 0;
}

#endif /* __APPLE__ */

static int xmp_link(const char *from, const char *to)
{
    qDebug() << "[xmp_link] path: " << from;

    int res;

    res = link(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

#ifdef __APPLE__

static int xmp_fsetattr_x(const char *path, struct setattr_x *attr,
              struct fuse_file_info *fi)
{
    qDebug() << "[xmp_fsetattr_x] path: " << path;

    int res;
    uid_t uid = -1;
    gid_t gid = -1;

    if (SETATTR_WANTS_MODE(attr)) {
        res = lchmod(path, attr->mode);
        if (res == -1)
            return -errno;
    }

    if (SETATTR_WANTS_UID(attr))
        uid = attr->uid;

    if (SETATTR_WANTS_GID(attr))
        gid = attr->gid;

    if ((uid != -1) || (gid != -1)) {
        res = lchown(path, uid, gid);
        if (res == -1)
            return -errno;
    }

    if (SETATTR_WANTS_SIZE(attr)) {
        if (fi)
            res = ftruncate(fi->fh, attr->size);
        else
            res = truncate(path, attr->size);
        if (res == -1)
            return -errno;
    }

    if (SETATTR_WANTS_MODTIME(attr)) {
        struct timeval tv[2];
        if (!SETATTR_WANTS_ACCTIME(attr))
            gettimeofday(&tv[0], NULL);
        else {
            tv[0].tv_sec = attr->acctime.tv_sec;
            tv[0].tv_usec = attr->acctime.tv_nsec / 1000;
        }
        tv[1].tv_sec = attr->modtime.tv_sec;
        tv[1].tv_usec = attr->modtime.tv_nsec / 1000;
        res = utimes(path, tv);
        if (res == -1)
            return -errno;
    }

    if (SETATTR_WANTS_CRTIME(attr)) {
        struct attrlist attributes;

        attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
        attributes.reserved = 0;
        attributes.commonattr = ATTR_CMN_CRTIME;
        attributes.dirattr = 0;
        attributes.fileattr = 0;
        attributes.forkattr = 0;
        attributes.volattr = 0;

        res = setattrlist(path, &attributes, &attr->crtime,
                  sizeof(struct timespec), FSOPT_NOFOLLOW);

        if (res == -1)
            return -errno;
    }

    if (SETATTR_WANTS_CHGTIME(attr)) {
        struct attrlist attributes;

        attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
        attributes.reserved = 0;
        attributes.commonattr = ATTR_CMN_CHGTIME;
        attributes.dirattr = 0;
        attributes.fileattr = 0;
        attributes.forkattr = 0;
        attributes.volattr = 0;

        res = setattrlist(path, &attributes, &attr->chgtime,
                  sizeof(struct timespec), FSOPT_NOFOLLOW);

        if (res == -1)
            return -errno;
    }

    if (SETATTR_WANTS_BKUPTIME(attr)) {
        struct attrlist attributes;

        attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
        attributes.reserved = 0;
        attributes.commonattr = ATTR_CMN_BKUPTIME;
        attributes.dirattr = 0;
        attributes.fileattr = 0;
        attributes.forkattr = 0;
        attributes.volattr = 0;

        res = setattrlist(path, &attributes, &attr->bkuptime,
                  sizeof(struct timespec), FSOPT_NOFOLLOW);

        if (res == -1)
            return -errno;
    }

    if (SETATTR_WANTS_FLAGS(attr)) {
        res = chflags(path, attr->flags);
        if (res == -1)
            return -errno;
    }

    return 0;
}

static int xmp_setattr_x(const char *path, struct setattr_x *attr)
{
    return xmp_fsetattr_x(path, attr, (struct fuse_file_info *)0);
}

static int xmp_chflags(const char *path, uint32_t flags)
{
    qDebug() << "[xmp_chflags] path: " << path;

    int res;

    res = chflags(path, flags);

    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_getxtimes(const char *path, struct timespec *bkuptime,
             struct timespec *crtime)
{
    qDebug() << "[xmp_getxtimes] path: " << path;

    int res = 0;
    struct attrlist attributes;

    attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
    attributes.reserved    = 0;
    attributes.commonattr  = 0;
    attributes.dirattr     = 0;
    attributes.fileattr    = 0;
    attributes.forkattr    = 0;
    attributes.volattr     = 0;



    struct xtimeattrbuf {
        uint32_t size;
        struct timespec xtime;
    } __attribute__ ((packed));


    struct xtimeattrbuf buf;

    attributes.commonattr = ATTR_CMN_BKUPTIME;
    res = getattrlist(path, &attributes, &buf, sizeof(buf), FSOPT_NOFOLLOW);
    if (res == 0)
        (void)memcpy(bkuptime, &(buf.xtime), sizeof(struct timespec));
    else
        (void)memset(bkuptime, 0, sizeof(struct timespec));

    attributes.commonattr = ATTR_CMN_CRTIME;
    res = getattrlist(path, &attributes, &buf, sizeof(buf), FSOPT_NOFOLLOW);
    if (res == 0)
        (void)memcpy(crtime, &(buf.xtime), sizeof(struct timespec));
    else
        (void)memset(crtime, 0, sizeof(struct timespec));

    return 0;
}

static int xmp_setbkuptime(const char *path, const struct timespec *bkuptime)
{
    qDebug() << "[xmp_setbkuptime] path: " << path;

    int res;

    struct attrlist attributes;

    attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
    attributes.reserved = 0;
    attributes.commonattr = ATTR_CMN_BKUPTIME;
    attributes.dirattr = 0;
    attributes.fileattr = 0;
    attributes.forkattr = 0;
    attributes.volattr = 0;

    res = setattrlist(path, &attributes, (void *)bkuptime,
              sizeof(struct timespec), FSOPT_NOFOLLOW);

    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_setchgtime(const char *path, const struct timespec *chgtime)
{
    qDebug() << "[xmp_setchgtime] path: " << path;

    int res;

    struct attrlist attributes;

    attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
    attributes.reserved = 0;
    attributes.commonattr = ATTR_CMN_CHGTIME;
    attributes.dirattr = 0;
    attributes.fileattr = 0;
    attributes.forkattr = 0;
    attributes.volattr = 0;

    res = setattrlist(path, &attributes, (void *)chgtime,
              sizeof(struct timespec), FSOPT_NOFOLLOW);

    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_setcrtime(const char *path, const struct timespec *crtime)
{
    qDebug() << "[xmp_setcrtime] path: " << path;

    int res;

    struct attrlist attributes;

    attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
    attributes.reserved = 0;
    attributes.commonattr = ATTR_CMN_CRTIME;
    attributes.dirattr = 0;
    attributes.fileattr = 0;
    attributes.forkattr = 0;
    attributes.volattr = 0;

    res = setattrlist(path, &attributes, (void *)crtime,
              sizeof(struct timespec), FSOPT_NOFOLLOW);

    if (res == -1)
        return -errno;

    return 0;
}

#endif /* __APPLE__ */

static int xmp_chmod(const char *path, mode_t mode)
{
    qDebug() << "[xmp_chmod] path: " << path;

    int res;

#ifdef __APPLE__
    res = lchmod(path, mode);
#else
    res = chmod(path, mode);
#endif
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid)
{
    qDebug() << "[xmp_chown] path: " << path;

    int res;

    res = lchown(path, uid, gid);
    if (res == -1)
        return -errno;

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

static int xmp_ftruncate(const char *path, off_t size,
             struct fuse_file_info *fi)
{
    qDebug() << "[xmp_ftruncate] path: " << path;

    int res;

    (void) path;

    res = ftruncate(fi->fh, size);
    if (res == -1)
        return -errno;

    return 0;
}

#ifdef HAVE_UTIMENSAT
static int xmp_utimens(const char *path, const struct timespec ts[2])
{
    qDebug() << "[xmp_utimens] path: " << path;

    int res;

    /* don't use utime/utimes since they follow symlinks */
    res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
    if (res == -1)
        return -errno;

    return 0;
}
#endif

static int xmp_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_create] path: " << path;

    struct fuse_context *context = fuse_get_context();
    FUSEClient *client = (FUSEClient *)context->private_data;
    assert(client && "[xmp_create] FUSEClient not found");

    qDebug() << "[xmp_create] flags:" << fi->flags;
    qDebug() << "[xmp_create] mmode:" << mode;

    Ref<CreateResult> result = client->FD_create(path, mode, fi->flags);

    qDebug() << "[xmp_create] status: " << result->status;

    return result->status;
}

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_open] path: " << path;

    (void)path;
    (void)fi;

//    int fd;

//    fd = open(path, fi->flags);
//    if (fd == -1)
//        return -errno;

//    fi->fh = fd;
    return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
    (void)fi;

    struct fuse_context *context = fuse_get_context();
    FUSEClient *client = (FUSEClient *)context->private_data;
    assert(client && "[xmp_read] FUSEClient not found");

    Ref<ReadResult> result = client->FD_read(path, size, offset);

    memset(buf, 0, size);

    if (result->status > 0)
    {
        memcpy(buf, result->data, result->status);
    }

    return result->status;
}

static int xmp_read_buf(const char *path, struct fuse_bufvec **bufp,
            size_t size, off_t offset, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_read_buf] path: " << path;

    struct fuse_bufvec *src;

    (void) path;

    src = (struct fuse_bufvec *)malloc(sizeof(struct fuse_bufvec));
    if (src == NULL)
        return -ENOMEM;

    *src = FUSE_BUFVEC_INIT(size);

    src->buf[0].flags = (enum fuse_buf_flags)(FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK);
    src->buf[0].fd = fi->fh;
    src->buf[0].pos = offset;

    *bufp = src;

    return 0;
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

    int res;

    (void) path;
    res = pwrite(fi->fh, buf, size, offset);
    if (res == -1)
        res = -errno;

    return res;
}

static int xmp_write_buf(const char *path, struct fuse_bufvec *buf,
             off_t offset, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_write_buf] path: " << path;

    struct fuse_bufvec dst = FUSE_BUFVEC_INIT(fuse_buf_size(buf));
    (void) path;

    dst.buf[0].flags = (enum fuse_buf_flags)(FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK);
    dst.buf[0].fd = fi->fh;
    dst.buf[0].pos = offset;

    return fuse_buf_copy(&dst, buf, FUSE_BUF_SPLICE_NONBLOCK);
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


}

static int xmp_flush(const char *path, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_flush] path: " << path;

    int res;

    (void) path;
    /* This is called from every close on an open file, so call the
       close on the underlying filesystem.	But since flush may be
       called multiple times for an open file, this must not really
       close the file.  This is important if used on a network
       filesystem like NFS which flush the data/metadata on close() */
    res = close(dup(fi->fh));
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_release] path: " << path;

    (void) path;
    (void) fi;
//    close(fi->fh);

    return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
             struct fuse_file_info *fi)
{
    qDebug() << "[xmp_fsync] path: " << path;

    int res;
    (void) path;

#ifndef HAVE_FDATASYNC
    (void) isdatasync;
#else
    if (isdatasync)
        res = fdatasync(fi->fh);
    else
#endif
        res = fsync(fi->fh);
    if (res == -1)
        return -errno;

    return 0;
}

#if defined(HAVE_POSIX_FALLOCATE) || defined(__APPLE__)
static int xmp_fallocate(const char *path, int mode,
            off_t offset, off_t length, struct fuse_file_info *fi)
{
    qDebug() << "[xmp_fallocate] path: " << path;

#ifdef __APPLE__
    fstore_t fstore;

    if (!(mode & PREALLOCATE))
        return -ENOTSUP;

    fstore.fst_flags = 0;
    if (mode & ALLOCATECONTIG)
        fstore.fst_flags |= F_ALLOCATECONTIG;
    if (mode & ALLOCATEALL)
        fstore.fst_flags |= F_ALLOCATEALL;

    if (mode & ALLOCATEFROMPEOF)
        fstore.fst_posmode = F_PEOFPOSMODE;
    else if (mode & ALLOCATEFROMVOL)
        fstore.fst_posmode = F_VOLPOSMODE;

    fstore.fst_offset = offset;
    fstore.fst_length = length;

    if (fcntl(fi->fh, F_PREALLOCATE, &fstore) == -1)
        return -errno;
    else
        return 0;
#else
    (void) path;

    if (mode)
        return -EOPNOTSUPP;

    return -posix_fallocate(fi->fh, offset, length);
#endif
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
#ifdef __APPLE__
static int xmp_setxattr(const char *path, const char *name, const char *value,
            size_t size, int flags, uint32_t position)
#else
static int xmp_setxattr(const char *path, const char *name, const char *value,
            size_t size, int flags)
#endif
{
    qDebug() << "[xmp_setxattr] path: " << path;

#ifdef __APPLE__
    int res;
    if (!strncmp(name, XATTR_APPLE_PREFIX, sizeof(XATTR_APPLE_PREFIX) - 1)) {
        flags &= ~(XATTR_NOSECURITY);
    }
    if (!strcmp(name, A_KAUTH_FILESEC_XATTR)) {
        char new_name[MAXPATHLEN];
        memcpy(new_name, A_KAUTH_FILESEC_XATTR, sizeof(A_KAUTH_FILESEC_XATTR));
        memcpy(new_name, G_PREFIX, sizeof(G_PREFIX) - 1);
        res = setxattr(path, new_name, value, size, position, flags);
    } else {
        res = setxattr(path, name, value, size, position, flags);
    }
#else
    int res = lsetxattr(path, name, value, size, flags);
#endif
    if (res == -1)
        return -errno;
    return 0;
}

#ifdef __APPLE__
static int xmp_getxattr(const char *path, const char *name, char *value,
            size_t size, uint32_t position)
#else
static int xmp_getxattr(const char *path, const char *name, char *value,
            size_t size)
#endif
{
    qDebug() << "[xmp_getxattr] path: " << path;

#ifdef __APPLE__
    int res;
    if (strcmp(name, A_KAUTH_FILESEC_XATTR) == 0) {
        char new_name[MAXPATHLEN];
        memcpy(new_name, A_KAUTH_FILESEC_XATTR, sizeof(A_KAUTH_FILESEC_XATTR));
        memcpy(new_name, G_PREFIX, sizeof(G_PREFIX) - 1);
        res = getxattr(path, new_name, value, size, position, XATTR_NOFOLLOW);
    } else {
        res = getxattr(path, name, value, size, position, XATTR_NOFOLLOW);
    }
#else
    int res = lgetxattr(path, name, value, size);
#endif
    if (res == -1)
        return -errno;
    return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
    qDebug() << "[xmp_listxattr] path: " << path;

#ifdef __APPLE__
    ssize_t res = listxattr(path, list, size, XATTR_NOFOLLOW);
    if (res > 0) {
        if (list) {
            size_t len = 0;
            char *curr = list;
            do {
                size_t thislen = strlen(curr) + 1;
                if (strcmp(curr, G_KAUTH_FILESEC_XATTR) == 0) {
                    memmove(curr, curr + thislen, res - len - thislen);
                    res -= thislen;
                    break;
                }
                curr += thislen;
                len += thislen;
            } while (len < res);
        } else {
            /*
            ssize_t res2 = getxattr(path, G_KAUTH_FILESEC_XATTR, NULL, 0, 0,
                        XATTR_NOFOLLOW);
            if (res2 >= 0) {
                res -= sizeof(G_KAUTH_FILESEC_XATTR);
            }
            */
        }
    }
#else
    int res = llistxattr(path, list, size);
#endif
    if (res == -1)
        return -errno;
    return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
    qDebug() << "[xmp_removexattr] path: " << path;

#ifdef __APPLE__
    int res;
    if (strcmp(name, A_KAUTH_FILESEC_XATTR) == 0) {
        char new_name[MAXPATHLEN];
        memcpy(new_name, A_KAUTH_FILESEC_XATTR, sizeof(A_KAUTH_FILESEC_XATTR));
        memcpy(new_name, G_PREFIX, sizeof(G_PREFIX) - 1);
        res = removexattr(path, new_name, XATTR_NOFOLLOW);
    } else {
        res = removexattr(path, name, XATTR_NOFOLLOW);
    }
#else
    int res = lremovexattr(path, name);
#endif
    if (res == -1)
        return -errno;
    return 0;
}
#endif /* HAVE_SETXATTR */

#ifndef __APPLE__
static int xmp_lock(const char *path, struct fuse_file_info *fi, int cmd,
            struct flock *lock)
{
    qDebug() << "[xmp_lock] path: " << path;

    (void) path;

    return ulockmgr_op(fi->fh, cmd, lock, &fi->lock_owner,
               sizeof(fi->lock_owner));
}
#endif

void *
xmp_init(struct fuse_conn_info *conn)
{
#ifdef __APPLE__
    FUSE_ENABLE_SETVOLNAME(conn);
    FUSE_ENABLE_XTIMES(conn);
#endif
    return NULL;
}

void
xmp_destroy(void *userdata)
{
}

#ifndef __APPLE__
static int xmp_flock(const char *path, struct fuse_file_info *fi, int op)
{
    qDebug() << "[xmp_flock] path: " << path;

    int res;
    (void) path;

    res = flock(fi->fh, op);
    if (res == -1)
        return -errno;

    return 0;
}
#endif

static struct fuse_operations xmp_oper = {
    // Minimal v1 operation set.
//    .init	   	= xmp_init,
    .destroy	= xmp_destroy,
    .getattr	= xmp_getattr,
//    .fgetattr	= xmp_fgetattr,
#ifndef __APPLE__
    .access		= xmp_access,
#endif
    .readlink	= xmp_readlink,
    .readdir	= xmp_readdir,
    .mkdir		= xmp_mkdir,
    .unlink		= xmp_unlink,
    .rmdir		= xmp_rmdir,
    .rename		= xmp_rename,
    .truncate	= xmp_truncate,
    .ftruncate	= xmp_ftruncate,
#ifdef HAVE_UTIMENSAT
    .utimens	= xmp_utimens,
#endif
    .create		= xmp_create,
    .open		= xmp_open,
    .read		= xmp_read,
    .write		= xmp_write,
    .statfs		= xmp_statfs,
//    .flush		= xmp_flush,
    .release	= xmp_release,
    .fsync		= xmp_fsync,

    .flag_nullpath_ok = 1,
#if HAVE_UTIMENSAT
    .flag_utime_omit_ok = 1,
#endif
};

// What was used during navigation:
//   xmp_statfs
//   xmp_getattr
//   xmp_fsetattr_x
//   xmp_opendir
// + xmp_readdir
//   xmp_releasedir
//   xmp_readlink

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
    char *argv[] = {"FileDonkey", "/Users/Guest/Public/fuse/", "-o", "volname=Windows  PC"};
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    int err = -1;

    if (fuse_parse_cmdline(&args, &mountpoint, NULL, NULL) != -1 &&
            (ch = fuse_mount(mountpoint, &args)) != NULL) {
            f = fuse_new(ch, &args, &xmp_oper, sizeof(xmp_oper), self->client);
            qDebug() << "before fuse_set_signal_handlers call";
            if (fuse_set_signal_handlers(fuse_get_session(f)) != 0) {
                fprintf(stderr, "Failed to set up signal handlers\n");
                perror("fuse_set_signal_handlers");
                fuse_destroy(f);
                fuse_unmount(mountpoint, ch);
                return;
            }
            qDebug() << "before fuse_loop call";
            fuse_loop(f);
            qDebug() << "after fuse_loop call";

            fuse_remove_signal_handlers(fuse_get_session(f));
            fuse_destroy(f);
            fuse_unmount(mountpoint, ch);
        }


}

void VirtDisk::mount(const QString &mountPoint)
{
    int argc = 4;
    char *argv[] = {"FileDonkey", "/Users/Guest/Public/fuse/", "-o", "volname=Windows PC"};
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);



    thread = std::thread(Start, this, &conn);
}

#endif
