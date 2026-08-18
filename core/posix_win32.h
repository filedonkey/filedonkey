#ifndef POSIX_WIN32_H
#define POSIX_WIN32_H

#if defined(_WIN32)

// A small POSIX layer for Windows: everything fusebackend.cpp calls by its POSIX name lives here,
// so that file needs no Windows knowledge beyond including this header.
//
// The names and signatures below are POSIX's own. WinFsp ships a sample that solves the same
// problem (passthrough-fuse3/winposix.c) and it is worth reading, but it is GPLv3 and this
// project is not, so no code is taken from it. Where the two end up alike it is because the Win32
// API admits one way to do the thing - a positioned read is an OVERLAPPED and nothing else.
//
// Paths are UTF-8 throughout, and every entry point widens them itself. The CRT's narrow calls
// speak the ANSI codepage, so any name outside it came back mangled - an en dash arrived as the
// single byte 0x96 - and then travelled the wire as invalid UTF-8.
//
// A file descriptor here is a Win32 HANDLE, not a CRT descriptor: open() hands one out and
// close(), pread() and pwrite() take it back. That keeps offsets 64 bit without seeking, and
// keeps us out of the CRT's descriptor table. -1 is still the error value, and a HANDLE is
// never -1.

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>

#include <fuse_win32.h>

// O_RDONLY and the rest come from <fcntl.h> above; open() below reads them as POSIX defines them.

// d_type values, as the shared readdir loop reads them: (d_type << 12) is the S_IF* file type.
#define DT_UNKNOWN  0
#define DT_DIR      4
#define DT_REG      8
#define DT_LNK      10

// NTFS caps a path component at 255 UTF-16 units. A UTF-16 unit costs at most three UTF-8 bytes
// (the four byte encodings come from surrogate pairs, which spend two units to get there), so 765
// bytes and a terminator is the true bound; rounded up for headroom.
#define FD_NAME_MAX 768

struct dirent
{
    uint64_t d_ino;
    unsigned char d_type;
    char d_name[FD_NAME_MAX];
};

typedef struct DirStream DIR;

int open(const char *path, int oflag, ...);
int close(int fd);
int pread(int fd, void *buf, size_t size, fuse_off_t offset);
int pwrite(int fd, const void *buf, size_t size, fuse_off_t offset);

int lstat(const char *path, struct fuse_stat *stbuf);
int statvfs(const char *path, struct fuse_statvfs *stbuf);
int readlink(const char *path, char *buf, size_t size);

int mkdir(const char *path, fuse_mode_t mode);
int rmdir(const char *path);
int unlink(const char *path);
int rename(const char *oldpath, const char *newpath);
int truncate(const char *path, fuse_off_t size);

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);

#endif // _WIN32

#endif // POSIX_WIN32_H
