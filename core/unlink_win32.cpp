#if defined(_WIN32)

#include "unlink_win32.h"

#include <errno.h>

static int win32_to_errno(DWORD err);

int unlink(const char *path)
{
    if (DeleteFileA(path))
        return 0;

    errno = win32_to_errno(GetLastError());
    return -1;
}

static int win32_to_errno(DWORD err)
{
    switch (err)
    {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
        return ENOENT;

    case ERROR_ACCESS_DENIED:
        return EACCES;

    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
        return EBUSY;

    case ERROR_WRITE_PROTECT:
        return EROFS;

    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS:
        return EEXIST;

    case ERROR_DIR_NOT_EMPTY:
        return ENOTEMPTY;

    case ERROR_NOT_SAME_DEVICE:
        return EXDEV;

    case ERROR_DISK_FULL:
        return ENOSPC;

    case ERROR_TOO_MANY_OPEN_FILES:
        return EMFILE;

    case ERROR_OUTOFMEMORY:
    case ERROR_NOT_ENOUGH_MEMORY:
        return ENOMEM;

    case ERROR_INVALID_NAME:
        return ENOENT;

    default:
        return EIO;
    }
}

#endif // _WIN32
