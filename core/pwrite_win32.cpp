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
        default:
            errno = EIO;
            break;
        }
        return -1;
    }

    return (ssize_t)bytesWritten;
}

#endif
