#ifndef PREAD_WIN32_H
#define PREAD_WIN32_H

#if defined(_WIN32)

#include <windows.h>
#include <fcntl.h>

ssize_t pread(int fd, void *buf, size_t size, off_t offset);
ssize_t win_pread_handle(HANDLE hFile, void *buf, size_t size, LARGE_INTEGER offset);
HANDLE fd_to_handle(int fd);
ssize_t win_pread_efficient(int fd, void *buf, size_t size, off_t offset);

#endif // _WIN32

#endif // PREAD_WIN32_H
