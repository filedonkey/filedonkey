#ifndef RENAME_WIN32_H
#define RENAME_WIN32_H

#if defined(_WIN32)

#include <windows.h>

int rename(const char *oldpath, const char *newpath);

#endif // _WIN32

#endif // RENAME_WIN32_H
