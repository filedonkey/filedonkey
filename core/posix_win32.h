#ifndef POSIX_WIN32_H
#define POSIX_WIN32_H

#if defined(_WIN32)

#include <winfsp_fuse.h>

int mkdir(const char *path, fuse_mode_t mode);
int rmdir(const char *path);

int rename(const char *oldpath, const char *newpath);
int unlink(const char *path);
int truncate(const char *path, fuse_off_t size);

#endif // _WIN32

#endif // POSIX_WIN32_H
