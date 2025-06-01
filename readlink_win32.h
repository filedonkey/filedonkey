#ifndef READLINK_WIN32_H
#define READLINK_WIN32_H

#if defined(__WIN32)

#include <windows.h>

ssize_t readlink(const char *path, char *buf, size_t size);

#endif // __WIN32

#endif // READLINK_WIN32_H
