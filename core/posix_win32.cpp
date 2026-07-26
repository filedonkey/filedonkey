#if defined(_WIN32)

#include <windows.h>

#include "posix_win32.h"

#include <errno.h>
#include <string>

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

static std::wstring to_utf8_wstr(const char *str)
{
    int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
    std::wstring wstr(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr.data(), len);

    return wstr;
}

int mkdir(const char *path, fuse_mode_t mode)
{
    (void)mode;

    std::wstring w_path = to_utf8_wstr(path);

    if (CreateDirectoryW(w_path.data(), 0))
    {
        return 0;
    }

    errno = win32_to_errno(GetLastError());
    return -1;
}

int rmdir(const char *path)
{
    std::wstring w_path = to_utf8_wstr(path);

    if (RemoveDirectoryW(w_path.data()))
    {
        return 0;
    }

    errno = win32_to_errno(GetLastError());
    return -1;
}

int rename(const char *oldpath, const char *newpath)
{
    std::wstring w_oldpath = to_utf8_wstr(oldpath);
    std::wstring w_newpath = to_utf8_wstr(newpath);

    if (MoveFileExW(w_oldpath.data(), w_newpath.data(), MOVEFILE_REPLACE_EXISTING))
    {
        return 0;
    }

    errno = win32_to_errno(GetLastError());
    return -1;
}

int unlink(const char *path)
{
    std::wstring w_path = to_utf8_wstr(path);

    if (DeleteFileW(w_path.data()))
    {
        return 0;
    }

    errno = win32_to_errno(GetLastError());
    return -1;
}

#endif // _WIN32
