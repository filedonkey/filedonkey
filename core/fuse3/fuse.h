#ifndef FUSE3_FUSE_H
#define FUSE3_FUSE_H

// The FUSE 3 API, for a platform with no libfuse of its own.
//
// fuse3_dokan.cpp implements all of it directly on dokan2.dll, the Dokan C API. There is no
// FUSE 2 anywhere in between: Dokany's own dokan_fuse wrapper stops at FUSE 2.7, and going through
// it would mean translating twice and carrying its struct layouts across a C++ ABI boundary.
// The names below are libfuse's, unchanged, so that the file system above is the same source on
// all three platforms - Linux and macOS include their real <fuse.h> and never see this file.
//
// Only what FileDonkey calls is implemented. The rest of struct fuse_operations is still declared,
// because it is initialised by designated initialiser and C++20 requires those in declaration
// order - dropping a member would silently shift the ones after it - but the operations this layer
// does not translate are reported to Windows as unsupported. See the table at the top of
// fuse3_dokan.cpp for what is wired up and what is not.

#include "fuse_win32.h"

#include <stddef.h>
#include <stdint.h>

struct fuse;
struct fuse_session;
struct fuse_conn_info;
struct fuse_config;
struct fuse_pollhandle;
struct fuse_bufvec;
struct fuse_flock;

// FUSE 3's own, not FUSE 2's: no fh_old, and the bitfields run further. Nothing outside this
// layer depends on the layout, but the file system reads fi->flags, so flags has to be first.
struct fuse_file_info
{
    int         flags;
    unsigned    writepage    : 1;
    unsigned    direct_io    : 1;
    unsigned    keep_cache   : 1;
    unsigned    flush        : 1;
    unsigned    nonseekable  : 1;
    unsigned    flock_release: 1;
    unsigned    cache_readdir: 1;
    unsigned    noflush      : 1;
    unsigned    padding      : 24;
    unsigned    padding2     : 32;
    uint64_t    fh;
    uint64_t    lock_owner;
    uint32_t    poll_events;
};

enum fuse_readdir_flags
{
    FUSE_READDIR_PLUS = (1 << 0),
};

enum fuse_fill_dir_flags
{
    FUSE_FILL_DIR_PLUS = (1 << 1),
};

typedef int (*fuse_fill_dir_t)(void *buf, const char *name, const struct fuse_stat *stbuf,
                              fuse_off_t off, enum fuse_fill_dir_flags flags);

// Declaration order is FUSE 3's. See the note at the top about why the unused members stay.
struct fuse_operations
{
    int (*getattr)(const char *, struct fuse_stat *, struct fuse_file_info *);
    int (*readlink)(const char *, char *, size_t);
    int (*mknod)(const char *, fuse_mode_t, fuse_dev_t);
    int (*mkdir)(const char *, fuse_mode_t);
    int (*unlink)(const char *);
    int (*rmdir)(const char *);
    int (*symlink)(const char *, const char *);
    int (*rename)(const char *, const char *, unsigned int);
    int (*link)(const char *, const char *);
    int (*chmod)(const char *, fuse_mode_t, struct fuse_file_info *);
    int (*chown)(const char *, fuse_uid_t, fuse_gid_t, struct fuse_file_info *);
    int (*truncate)(const char *, fuse_off_t, struct fuse_file_info *);
    int (*open)(const char *, struct fuse_file_info *);
    int (*read)(const char *, char *, size_t, fuse_off_t, struct fuse_file_info *);
    int (*write)(const char *, const char *, size_t, fuse_off_t, struct fuse_file_info *);
    int (*statfs)(const char *, struct fuse_statvfs *);
    int (*flush)(const char *, struct fuse_file_info *);
    int (*release)(const char *, struct fuse_file_info *);
    int (*fsync)(const char *, int, struct fuse_file_info *);
    int (*setxattr)(const char *, const char *, const char *, size_t, int);
    int (*getxattr)(const char *, const char *, char *, size_t);
    int (*listxattr)(const char *, char *, size_t);
    int (*removexattr)(const char *, const char *);
    int (*opendir)(const char *, struct fuse_file_info *);
    int (*readdir)(const char *, void *, fuse_fill_dir_t, fuse_off_t, struct fuse_file_info *,
                   enum fuse_readdir_flags);
    int (*releasedir)(const char *, struct fuse_file_info *);
    int (*fsyncdir)(const char *, int, struct fuse_file_info *);
    void *(*init)(struct fuse_conn_info *, struct fuse_config *);
    void (*destroy)(void *);
    int (*access)(const char *, int);
    int (*create)(const char *, fuse_mode_t, struct fuse_file_info *);
    int (*lock)(const char *, struct fuse_file_info *, int, struct fuse_flock *);
    int (*utimens)(const char *, const struct fuse_timespec[2], struct fuse_file_info *);
    int (*bmap)(const char *, size_t, uint64_t *);
    int (*ioctl)(const char *, int, void *, struct fuse_file_info *, unsigned int, void *);
    int (*poll)(const char *, struct fuse_file_info *, struct fuse_pollhandle *, unsigned *);
    int (*write_buf)(const char *, struct fuse_bufvec *, fuse_off_t, struct fuse_file_info *);
    int (*read_buf)(const char *, struct fuse_bufvec **, size_t, fuse_off_t, struct fuse_file_info *);
    int (*flock)(const char *, struct fuse_file_info *, int);
    int (*fallocate)(const char *, int, fuse_off_t, fuse_off_t, struct fuse_file_info *);
    ssize_t (*copy_file_range)(const char *, struct fuse_file_info *, fuse_off_t, const char *,
                               struct fuse_file_info *, fuse_off_t, size_t, int);
    fuse_off_t (*lseek)(const char *, fuse_off_t, int, struct fuse_file_info *);
};

// Dokany's layout, which has no umask member. Nothing here copies the struct - fuse_get_context()
// hands back the one Dokany owns - so it only has to describe the same bytes.
struct fuse_context
{
    struct fuse *fuse;
    fuse_uid_t  uid;
    fuse_gid_t  gid;
    int         pid;
    void       *private_data;
};

// Identical in FUSE 2 and FUSE 3, and shared with Dokany as it stands.
struct fuse_args
{
    int    argc;
    char **argv;
    int    allocated;
};

#define FUSE_ARGS_INIT(argc, argv) { argc, argv, 0 }

#ifdef __cplusplus
extern "C" {
#endif

struct fuse *fuse_new(struct fuse_args *args, const struct fuse_operations *op, size_t op_size,
                    void *private_data);
struct fuse_session *fuse_get_session(struct fuse *f);
int   fuse_mount(struct fuse *f, const char *mountpoint);
void  fuse_unmount(struct fuse *f);
int   fuse_loop(struct fuse *f);
void  fuse_exit(struct fuse *f);
void  fuse_destroy(struct fuse *f);
int   fuse_set_signal_handlers(struct fuse_session *se);
void  fuse_remove_signal_handlers(struct fuse_session *se);
struct fuse_context *fuse_get_context(void);
void  fuse_opt_free_args(struct fuse_args *args);

#ifdef __cplusplus
}
#endif

#endif // FUSE3_FUSE_H
