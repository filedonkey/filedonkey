#if defined(_WIN32)

#include "pwrite_win32.h"

#include <io.h>
#include <errno.h>

ssize_t pwrite(int fd, const void *buffer, size_t count, off_t offset)
{
    // Get Windows file handle from file descriptor
    HANDLE hFile = (HANDLE)_get_osfhandle(fd);
    if (hFile == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }

    // Validate parameters
    if (buffer == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (offset < 0) {
        errno = EINVAL;
        return -1;
    }

    // Create OVERLAPPED structure for positioned I/O
    OVERLAPPED overlapped = {0};
    overlapped.Offset = (DWORD)(offset & 0xFFFFFFFF);
    overlapped.OffsetHigh = (DWORD)((offset >> 32) & 0xFFFFFFFF);

    // Perform the write operation
    DWORD bytesWritten = 0;
    BOOL result = WriteFile(hFile, buffer, (DWORD)count, &bytesWritten, &overlapped);

    if (!result) {
        DWORD error = GetLastError();
        switch (error) {
        case ERROR_INVALID_HANDLE:
        case ERROR_ACCESS_DENIED:
            errno = EBADF;
            break;
        case ERROR_NOT_ENOUGH_MEMORY:
            errno = ENOMEM;
            break;
        case ERROR_DISK_FULL:
            errno = ENOSPC;
            break;
        case ERROR_HANDLE_DISK_FULL:
            errno = ENOSPC;
            break;
        case ERROR_INVALID_PARAMETER:
            errno = EINVAL;
            break;
        case ERROR_INVALID_USER_BUFFER:
            errno = EFAULT;
            break;
        default:
            errno = EIO;
            break;
        }
        return -1;
    }

    return (ssize_t)bytesWritten;
}

ssize_t pwrite_non_overlapped(int fd, const void *buffer, size_t count, off_t offset)
{
    HANDLE hFile = (HANDLE)_get_osfhandle(fd);
    if (hFile == INVALID_HANDLE_VALUE) { errno = EBADF; return -1; }
    if (!buffer)    { errno = EFAULT;  return -1; }
    if (offset < 0) { errno = EINVAL;  return -1; }

    // Save current position
    LARGE_INTEGER liOld = {0}, liNew = {0};
    if (!SetFilePointerEx(hFile, liOld, &liOld, FILE_CURRENT)) {
        errno = EIO; return -1;
    }

    // Seek to target offset
    LARGE_INTEGER liTarget;
    liTarget.QuadPart = offset;
    if (!SetFilePointerEx(hFile, liTarget, NULL, FILE_BEGIN)) {
        errno = EIO; return -1;
    }

    // Write
    DWORD bytesWritten = 0;
    BOOL result = WriteFile(hFile, buffer, (DWORD)count, &bytesWritten, NULL); // NULL = no OVERLAPPED

    // Restore original position
    SetFilePointerEx(hFile, liOld, NULL, FILE_BEGIN);

    if (!result) {
        DWORD error = GetLastError();
        qDebug() << "pwrite_non_overlapped last error:" << error;
        switch (error) {
        case ERROR_ACCESS_DENIED:
        case ERROR_INVALID_HANDLE: errno = EBADF;  break;
        case ERROR_DISK_FULL:
        case ERROR_HANDLE_DISK_FULL: errno = ENOSPC; break;
        case ERROR_INVALID_USER_BUFFER: errno = EFAULT; break;
        default: errno = EIO; break;
        }
        return -1;
    }

    return (ssize_t)bytesWritten;
}

#endif
