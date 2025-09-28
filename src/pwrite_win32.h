#ifndef PWRITE_WIN32_H
#define PWRITE_WIN32_H

#if defined(_WIN32)

#include <windows.h>

ssize_t pwrite(int fd, const void *buffer, size_t count, off_t offset);

#endif // _WIN32

#endif // PWRITE_WIN32_H
