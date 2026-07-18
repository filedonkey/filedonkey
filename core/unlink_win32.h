#ifndef UNLINK_WIN32_H
#define UNLINK_WIN32_H

#if defined(_WIN32)

#include <windows.h>

int unlink(const char *path);

#endif // _WIN32

#endif // UNLINK_WIN32_H
