#if defined(__WIN32)

#include "readlink_win32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
// #include <winioctl.h>

// Define reparse point structures if not available
#ifndef IO_REPARSE_TAG_SYMLINK
#define IO_REPARSE_TAG_SYMLINK (0xA000000CL)
#endif

#ifndef IO_REPARSE_TAG_MOUNT_POINT
#define IO_REPARSE_TAG_MOUNT_POINT (0xA0000003L)
#endif

#ifndef FSCTL_GET_REPARSE_POINT
#define FSCTL_GET_REPARSE_POINT CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 42, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#ifndef MAXIMUM_REPARSE_DATA_BUFFER_SIZE
#define MAXIMUM_REPARSE_DATA_BUFFER_SIZE (16 * 1024)
#endif

// Define reparse data buffer structures
typedef struct _REPARSE_DATA_BUFFER {
    ULONG  ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    union {
        struct {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            ULONG  Flags;
            WCHAR  PathBuffer[1];
        } SymbolicLinkReparseBuffer;
        struct {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            WCHAR  PathBuffer[1];
        } MountPointReparseBuffer;
        struct {
            UCHAR DataBuffer[1];
        } GenericReparseBuffer;
    };
} REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;

ssize_t readlink(const char *path, char *buf, size_t size) {
    HANDLE hFile;
    DWORD dwReparseTag;
    REPARSE_DATA_BUFFER *reparseBuffer;
    DWORD bytesReturned;
    WCHAR *linkTarget;
    ssize_t result = -1;
    WCHAR widePath[MAX_PATH];
    DWORD bufferSize;

    // Convert path to wide character
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, widePath, MAX_PATH) == 0) {
        errno = EINVAL;
        return -1;
    }

    // Open the file/directory for reading attributes
    hFile = CreateFileW(
        widePath,
        0,  // No access required for reading reparse point
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        NULL
        );

    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        switch (error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            errno = ENOENT;
            break;
        case ERROR_ACCESS_DENIED:
            errno = EACCES;
            break;
        default:
            errno = EIO;
            break;
        }
        return -1;
    }

    // Allocate buffer for reparse data
    bufferSize = MAXIMUM_REPARSE_DATA_BUFFER_SIZE;
    reparseBuffer = (REPARSE_DATA_BUFFER*)malloc(bufferSize);
    if (!reparseBuffer) {
        CloseHandle(hFile);
        errno = ENOMEM;
        return -1;
    }

    // Get the reparse point data
    if (!DeviceIoControl(
            hFile,
            FSCTL_GET_REPARSE_POINT,
            NULL, 0,
            reparseBuffer, bufferSize,
            &bytesReturned,
            NULL)) {

        DWORD error = GetLastError();
        free(reparseBuffer);
        CloseHandle(hFile);

        if (error == ERROR_NOT_A_REPARSE_POINT) {
            errno = EINVAL;  // Not a symbolic link
        } else {
            errno = EIO;
        }
        return -1;
    }

    dwReparseTag = reparseBuffer->ReparseTag;

    // Handle different types of reparse points
    if (dwReparseTag == IO_REPARSE_TAG_SYMLINK) {
        // Symbolic link
        linkTarget = (WCHAR*)((BYTE*)reparseBuffer->SymbolicLinkReparseBuffer.PathBuffer +
                                reparseBuffer->SymbolicLinkReparseBuffer.PrintNameOffset);

        // Convert wide character target to UTF-8
        int targetLen = WideCharToMultiByte(
            CP_UTF8, 0,
            linkTarget,
            reparseBuffer->SymbolicLinkReparseBuffer.PrintNameLength / sizeof(WCHAR),
            buf, (int)size - 1,  // Leave space for null terminator
            NULL, NULL
            );

        if (targetLen > 0) {
            buf[targetLen] = '\0';
            result = targetLen;
        } else {
            errno = EILSEQ;  // Invalid character sequence
        }

    } else if (dwReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
        // Junction point (mount point)
        linkTarget = (WCHAR*)((BYTE*)reparseBuffer->MountPointReparseBuffer.PathBuffer +
                                reparseBuffer->MountPointReparseBuffer.PrintNameOffset);

        // Convert wide character target to UTF-8
        int targetLen = WideCharToMultiByte(
            CP_UTF8, 0,
            linkTarget,
            reparseBuffer->MountPointReparseBuffer.PrintNameLength / sizeof(WCHAR),
            buf, (int)size - 1,  // Leave space for null terminator
            NULL, NULL
            );

        if (targetLen > 0) {
            buf[targetLen] = '\0';
            result = targetLen;
        } else {
            errno = EILSEQ;  // Invalid character sequence
        }

    } else {
        // Unknown reparse point type
        errno = EINVAL;
    }

    free(reparseBuffer);
    CloseHandle(hFile);

    return result;
}

// Helper function to check if a path is a symbolic link
int is_symlink(const char *path) {
    DWORD attrs = GetFileAttributesA(path);

    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return 0;  // File doesn't exist or can't be accessed
    }

    return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

// Simplified readlink implementation using GetFinalPathNameByHandle
// ssize_t readlink(const char *path, char *buf, size_t size) {
//     HANDLE hFile;
//     WCHAR widePath[MAX_PATH];
//     WCHAR targetPath[MAX_PATH];
//     DWORD result;
//     int targetLen;

//     // Convert path to wide character
//     if (MultiByteToWideChar(CP_UTF8, 0, path, -1, widePath, MAX_PATH) == 0) {
//         errno = EINVAL;
//         return -1;
//     }

//     // Open the file
//     hFile = CreateFileW(
//         widePath,
//         0,
//         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
//         NULL,
//         OPEN_EXISTING,
//         FILE_FLAG_BACKUP_SEMANTICS,
//         NULL
//         );

//     if (hFile == INVALID_HANDLE_VALUE) {
//         DWORD error = GetLastError();
//         switch (error) {
//         case ERROR_FILE_NOT_FOUND:
//         case ERROR_PATH_NOT_FOUND:
//             errno = ENOENT;
//             break;
//         case ERROR_ACCESS_DENIED:
//             errno = EACCES;
//             break;
//         default:
//             errno = EIO;
//             break;
//         }
//         return -1;
//     }

//     // Get the final path name (resolves symbolic links)
//     result = GetFinalPathNameByHandleW(
//         hFile,
//         targetPath,
//         MAX_PATH,
//         FILE_NAME_NORMALIZED | VOLUME_NAME_DOS
//         );

//     CloseHandle(hFile);

//     if (result == 0 || result >= MAX_PATH) {
//         errno = EIO;
//         return -1;
//     }

//     // Remove \\?\ prefix if present
//     WCHAR *finalPath = targetPath;
//     if (wcsncmp(targetPath, L"\\\\?\\", 4) == 0) {
//         finalPath = targetPath + 4;
//     }

//     // Convert back to UTF-8
//     targetLen = WideCharToMultiByte(
//         CP_UTF8, 0,
//         finalPath, -1,
//         buf, (int)size,
//         NULL, NULL
//         );

//     if (targetLen <= 0) {
//         errno = EILSEQ;
//         return -1;
//     }

//     return targetLen - 1; // Subtract 1 for null terminator
// }

// Example usage and test function
// void test_readlink() {
//     char target[MAX_PATH];
//     ssize_t len;

//     // Test with a symbolic link (you'll need to create one first)
//     const char *test_link = "test_symlink";

//     printf("Testing readlink with: %s\n", test_link);

//     if (is_symlink(test_link)) {
//         len = readlink(test_link, target, sizeof(target));
//         if (len >= 0) {
//             printf("Link target: %s (length: %zd)\n", target, len);
//         } else {
//             perror("readlink failed");
//         }
//     } else {
//         printf("File is not a symbolic link or doesn't exist\n");
//     }
// }

#endif
