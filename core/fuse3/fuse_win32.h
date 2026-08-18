#ifndef FUSE3_FUSE_WIN32_H
#define FUSE3_FUSE_WIN32_H

#if !defined(_WIN32)
#error fuse_win32.h is the Windows half of the FUSE 3 layer; Linux and macOS use their own libfuse3
#endif

// The POSIX types the FUSE 3 API is written in, for a platform that has none of them.
//
// Every field is a fixed width type rather than the CRT's own mode_t, nlink_t and friends, which
// on Windows are whatever a given compiler decided: MinGW's mode_t is two bytes and MSVC's is
// four, its ino_t is two bytes against a real inode's eight, and its off_t is 32 bit unless a
// feature macro says otherwise. None of that is a boundary this layer can afford to be vague
// about - st_size is a file length and st_ino is what tells Windows two paths are the same file.
//
// The layout matches WinFsp's struct fuse_stat field for field, which is what the code filling
// these structs was written against, so posix_win32.cpp and fusebackend.cpp needed no changes to
// move off it. Nothing outside this project depends on it: fuse3_dokan.cpp reads these structs and
// hands Dokan its own Windows ones, so the layout is now entirely ours.

#include <stddef.h>
#include <stdint.h>

typedef uint32_t fuse_dev_t;
typedef uint64_t fuse_ino_t;
typedef uint32_t fuse_mode_t;
typedef uint16_t fuse_nlink_t;
typedef uint32_t fuse_uid_t;
typedef uint32_t fuse_gid_t;
typedef int64_t  fuse_off_t;
typedef int32_t  fuse_blksize_t;
typedef int64_t  fuse_blkcnt_t;
typedef uint64_t fuse_fsblkcnt_t;
typedef uint64_t fuse_fsfilcnt_t;

// MinGW hides ssize_t behind __STRICT_ANSI__, and the FUSE 3 prototypes need it.
#if !defined(_SSIZE_T_DEFINED)
#define _SSIZE_T_DEFINED
typedef intptr_t ssize_t;
#endif

// tv_nsec is four bytes followed by four of padding, because that is what a `long` is on Windows
// and Dokany's timestruc_t is the CRT's struct timespec. The padding is named so the size is
// obvious rather than implied. Note this differs from WinFsp, which makes tv_nsec a full int64;
// the two are interchangeable in practice only because a nanosecond count fits in 30 bits and the
// machine is little endian, so the meaningful half lands in the same four bytes either way.
struct fuse_timespec
{
    int64_t tv_sec;
    int32_t tv_nsec;
    int32_t __pad_tv_nsec;
};

struct fuse_stat
{
    fuse_dev_t              st_dev;
    fuse_ino_t              st_ino;
    fuse_mode_t             st_mode;
    fuse_nlink_t            st_nlink;
    fuse_uid_t              st_uid;
    fuse_gid_t              st_gid;
    fuse_dev_t              st_rdev;
    fuse_off_t              st_size;
    struct fuse_timespec    st_atim;
    struct fuse_timespec    st_mtim;
    struct fuse_timespec    st_ctim;
    fuse_blksize_t          st_blksize;
    fuse_blkcnt_t           st_blocks;
    struct fuse_timespec    st_birthtim;
};

struct fuse_statvfs
{
    uint32_t                f_bsize;
    uint32_t                f_frsize;
    fuse_fsblkcnt_t         f_blocks;
    fuse_fsblkcnt_t         f_bfree;
    fuse_fsblkcnt_t         f_bavail;
    fuse_fsfilcnt_t         f_files;
    fuse_fsfilcnt_t         f_ffree;
    fuse_fsfilcnt_t         f_favail;
    uint32_t                f_fsid;
    uint32_t                f_flag;
    uint32_t                f_namemax;
};

#endif // FUSE3_FUSE_WIN32_H
