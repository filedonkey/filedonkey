#if defined(_WIN32)

#include "pread_win32.h"

#include <io.h>
#include <errno.h>
#include <sys/types.h>
#include <stdio.h>

// Define off_t if not already defined
#ifndef _OFF_T_DEFINED
typedef long long off_t;
#define _OFF_T_DEFINED
#endif

/**
 * NOTE: It has issues. Do not use it until it fixed.
 *
 * Windows implementation of the POSIX pread function
 *
 * @param fd     File descriptor (from open)
 * @param buf    Buffer to read data into
 * @param size   Number of bytes to read
 * @param offset Offset in the file to read from
 * @return       Number of bytes read, or -1 on error
 */
ssize_t pread(int fd, void *buf, size_t size, off_t offset) {
    if (fd < 0 || buf == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Get the current file position
    off_t current_pos = _lseeki64(fd, 0, SEEK_CUR);
    if (current_pos == -1) {
        return -1;  // errno already set by _lseeki64
    }

    // Set file pointer to desired offset
    if (_lseeki64(fd, offset, SEEK_SET) == -1) {
        return -1;  // errno already set by _lseeki64
    }

    // Read the data
    ssize_t bytes_read = _read(fd, buf, (unsigned int)size);

    // Restore the original file position
    if (_lseeki64(fd, current_pos, SEEK_SET) == -1) {
        if (bytes_read != -1) {
            // Save the read error if we succeeded in reading
            int saved_errno = errno;
            bytes_read = -1;
            errno = saved_errno;
        }
    }

    return bytes_read;
}

/**
 * Alternative implementation using Windows API directly (for HANDLE-based files)
 *
 * @param hFile   Windows file HANDLE
 * @param buf     Buffer to read data into
 * @param size    Number of bytes to read
 * @param offset  Offset in the file to read from
 * @return        Number of bytes read, or -1 on error
 */
ssize_t win_pread_handle(HANDLE hFile, void *buf, size_t size, LARGE_INTEGER offset) {
    if (hFile == INVALID_HANDLE_VALUE || buf == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }

    OVERLAPPED overlapped = {0};
    overlapped.Offset = offset.LowPart;
    overlapped.OffsetHigh = offset.HighPart;

    DWORD bytes_read = 0;
    BOOL result = ReadFile(hFile, buf, (DWORD)size, &bytes_read, &overlapped);

    if (!result) {
        return -1;
    }

    return (ssize_t)bytes_read;
}

/**
 * Helper function to convert a file descriptor to a Windows HANDLE
 *
 * @param fd  File descriptor
 * @return    Windows HANDLE, or INVALID_HANDLE_VALUE on error
 */
HANDLE fd_to_handle(int fd) {
    if (fd < 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }

    // Get operating system file handle from C runtime file descriptor
    return (HANDLE)_get_osfhandle(fd);
}

/**
 * More efficient pread implementation that uses Windows API directly
 *
 * @param fd     File descriptor (from open)
 * @param buf    Buffer to read data into
 * @param size   Number of bytes to read
 * @param offset Offset in the file to read from
 * @return       Number of bytes read, or -1 on error
 */
ssize_t win_pread_efficient(int fd, void *buf, size_t size, off_t offset) {
    // Convert file descriptor to Windows HANDLE
    HANDLE hFile = fd_to_handle(fd);
    if (hFile == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }

    LARGE_INTEGER li_offset;
    li_offset.QuadPart = offset;

    DWORD bytes_read = 0;
    OVERLAPPED overlapped = {0};
    overlapped.Offset = li_offset.LowPart;
    overlapped.OffsetHigh = li_offset.HighPart;

    if (!ReadFile(hFile, buf, (DWORD)size, &bytes_read, &overlapped)) {
        DWORD error = GetLastError();
        if (error == ERROR_HANDLE_EOF) {
            // End of file reached
            return 0;
        }

        // Map Windows error to errno
        switch (error) {
        case ERROR_ACCESS_DENIED:
            errno = EACCES;
            break;
        case ERROR_INVALID_HANDLE:
            errno = EBADF;
            break;
        case ERROR_NOT_ENOUGH_MEMORY:
            errno = ENOMEM;
            break;
        default:
            errno = EIO;
            break;
        }
        return -1;
    }

    return (ssize_t)bytes_read;
}

// Example usage
// #include <stdlib.h>
// #include <string.h>

// int main(int argc, char *argv[]) {
//     if (argc < 2) {
//         fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
//         return 1;
//     }

//     const char* filename = argv[1];
//     int fd = _open(filename, _O_RDONLY | _O_BINARY);
//     if (fd == -1) {
//         perror("Failed to open file");
//         return 1;
//     }

//     // Allocate buffer
//     char buffer[1024];
//     memset(buffer, 0, sizeof(buffer));

//     // Read 100 bytes from offset 50
//     ssize_t bytes_read = win_pread_efficient(fd, buffer, 100, 50);
//     if (bytes_read == -1) {
//         perror("win_pread failed");
//         _close(fd);
//         return 1;
//     }

//     printf("Read %zd bytes from offset 50:\n", bytes_read);

//     // Null-terminate the buffer for safe printing (assuming it's text)
//     if (bytes_read < sizeof(buffer)) {
//         buffer[bytes_read] = '\0';
//     } else {
//         buffer[sizeof(buffer) - 1] = '\0';
//     }

//     // Print the data (if it's text)
//     printf("%s\n", buffer);

//     _close(fd);
//     return 0;
// }

#endif
