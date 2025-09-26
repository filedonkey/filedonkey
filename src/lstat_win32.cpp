#if defined(__WIN32)

#include <windows.h>
#include <fuse.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/timeb.h>
#include <time.h>
#include <io.h>

#include <QRandomGenerator>

// Define constants similar to POSIX file types
#define WIN_S_IFMT   0170000  // bit mask for the file type bit field
#define WIN_S_IFSOCK 0140000  // socket
#define WIN_S_IFLNK  0120000  // symbolic link
#define WIN_S_IFREG  0100000  // regular file
#define WIN_S_IFBLK  0060000  // block device
#define WIN_S_IFDIR  0040000  // directory
#define WIN_S_IFCHR  0020000  // character device
#define WIN_S_IFIFO  0010000  // FIFO

// Permission bits
#define WIN_S_ISUID  0004000  // set UID bit
#define WIN_S_ISGID  0002000  // set-group-ID bit
#define WIN_S_ISVTX  0001000  // sticky bit
#define WIN_S_IRWXU  0000700  // owner has read, write, and execute permission
#define WIN_S_IRUSR  0000400  // owner has read permission
#define WIN_S_IWUSR  0000200  // owner has write permission
#define WIN_S_IXUSR  0000100  // owner has execute permission
#define WIN_S_IRWXG  0000070  // group has read, write, and execute permission
#define WIN_S_IRGRP  0000040  // group has read permission
#define WIN_S_IWGRP  0000020  // group has write permission
#define WIN_S_IXGRP  0000010  // group has execute permission
#define WIN_S_IRWXO  0000007  // others have read, write, and execute permission
#define WIN_S_IROTH  0000004  // others have read permission
#define WIN_S_IWOTH  0000002  // others have write permission
#define WIN_S_IXOTH  0000001  // others have execute permission

// Macros to test file types
#define WIN_S_ISREG(m)  (((m) & WIN_S_IFMT) == WIN_S_IFREG)
#define WIN_S_ISDIR(m)  (((m) & WIN_S_IFMT) == WIN_S_IFDIR)
#define WIN_S_ISCHR(m)  (((m) & WIN_S_IFMT) == WIN_S_IFCHR)
#define WIN_S_ISBLK(m)  (((m) & WIN_S_IFMT) == WIN_S_IFBLK)
#define WIN_S_ISFIFO(m) (((m) & WIN_S_IFMT) == WIN_S_IFIFO)
#define WIN_S_ISLNK(m)  (((m) & WIN_S_IFMT) == WIN_S_IFLNK)
#define WIN_S_ISSOCK(m) (((m) & WIN_S_IFMT) == WIN_S_IFSOCK)

// Function to convert Windows FILETIME to timestruc_t
void FileTimeToTimestruc(const FILETIME &ft, timestruc_t &ts) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;

    // Convert from 100ns intervals to seconds and nanoseconds
    // Windows FILETIME: January 1, 1601 (UTC)
    // Unix timestamp: January 1, 1970 (UTC)
    // Difference: 11644473600 seconds

    // Calculate seconds
    ts.tv_sec = (time_t)((uli.QuadPart / 10000000ULL) - 11644473600ULL);

    // Calculate nanoseconds (remaining 100ns intervals * 100)
    ts.tv_nsec = (long)((uli.QuadPart % 10000000ULL) * 100);
}

// Windows equivalent of lstat
int lstat(const char* path, struct FUSE_STAT* buf) {
    if (!path || !buf) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }

    // Initialize the structure
    memset(buf, 0, sizeof(struct FUSE_STAT));

    // Convert to wide string for Unicode API
    int wchars_needed = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (wchars_needed <= 0) {
        return -1;
    }

    wchar_t* wpath = new wchar_t[wchars_needed];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wchars_needed) <= 0) {
        delete[] wpath;
        return -1;
    }

    WIN32_FILE_ATTRIBUTE_DATA fileData;
    BOOL success = GetFileAttributesExW(wpath, GetFileExInfoStandard, &fileData);

    // Check if file exists and get its attributes
    if (!success) {
        delete[] wpath;
        return -1;
    }

    // First, check if it's a reparse point (potential symlink)
    bool isSymLink = (fileData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

    // If it's a symlink, we need more specific info to confirm it's a symlink and not another type of reparse point
    if (isSymLink) {
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(wpath, &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            // Check if it's specifically a symlink and not another type of reparse point
            isSymLink = (findData.dwReserved0 == IO_REPARSE_TAG_SYMLINK);
            FindClose(hFind);
        }
    }

    // Setup basic file information
    ULARGE_INTEGER fileSize;
    fileSize.LowPart = fileData.nFileSizeLow;
    fileSize.HighPart = fileData.nFileSizeHigh;

    // Set file mode/type
    buf->st_mode = 0;
    if (isSymLink) {
        buf->st_mode |= WIN_S_IFLNK;
    } else if (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        buf->st_mode |= WIN_S_IFDIR;
    } else {
        buf->st_mode |= WIN_S_IFREG;
    }

    // Set permissions (Windows doesn't match POSIX permissions exactly)
    // Assume owner has full permissions, group and others have read permission
    // buf->st_mode |= WIN_S_IRWXU;  // Owner can read/write/execute
    buf->st_mode |= WIN_S_IRUSR; // Owner can read
    buf->st_mode |= WIN_S_IWUSR; // Owner can write

    if (!(fileData.dwFileAttributes & FILE_ATTRIBUTE_READONLY)) {
        buf->st_mode |= WIN_S_IRGRP | WIN_S_IROTH;  // Group and others can read
    }

    if (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        buf->st_mode |= WIN_S_IXGRP | WIN_S_IXOTH | WIN_S_IRGRP | WIN_S_IROTH;  // If directory, also give execute permission
    }

    // Hard to determine number of links in Windows, assume 1
    buf->st_nlink = 1;

    // Windows doesn't have direct user/group ID concept like POSIX
    buf->st_uid = 0;
    buf->st_gid = 0;

    // Set file size
    buf->st_size = fileSize.QuadPart;

    // Set timestamps
    FileTimeToTimestruc(fileData.ftLastAccessTime, buf->st_atim);
    FileTimeToTimestruc(fileData.ftLastWriteTime, buf->st_mtim);
    FileTimeToTimestruc(fileData.ftLastWriteTime, buf->st_ctim);
    // In Windows, ftCreationTime is the birth time
    FileTimeToTimestruc(fileData.ftCreationTime, buf->st_birthtim);

    // Not directly available in Windows
    buf->st_dev = 0;
    buf->st_ino = QRandomGenerator::global()->generate64();
    buf->st_rdev = 0;

    // Use some sensible defaults for block info
    buf->st_blksize = 4096;  // Common block size
    buf->st_blocks = (fileSize.QuadPart + 511) / 512;  // Round up to 512 byte blocks
    buf->st_blocks += 1; // For some reason on linux and macos this value is 256, but on windows it's 255

    delete[] wpath;
    return 0;
}

// Example usage
// int main(int argc, char** argv) {
//     const char* path = argc > 1 ? argv[1] : ".";
//     struct FUSE_STAT st;

//     if (win_lstat(path, &st) == 0) {
//         std::cout << "File information for " << path << ":\n";

//         // Print file type
//         std::cout << "File type: ";
//         if (WIN_S_ISREG(st.st_mode)) std::cout << "regular file\n";
//         else if (WIN_S_ISDIR(st.st_mode)) std::cout << "directory\n";
//         else if (WIN_S_ISLNK(st.st_mode)) std::cout << "symbolic link\n";
//         else if (WIN_S_ISCHR(st.st_mode)) std::cout << "character device\n";
//         else if (WIN_S_ISBLK(st.st_mode)) std::cout << "block device\n";
//         else if (WIN_S_ISFIFO(st.st_mode)) std::cout << "FIFO/pipe\n";
//         else if (WIN_S_ISSOCK(st.st_mode)) std::cout << "socket\n";
//         else std::cout << "unknown type\n";

//         std::cout << "Size: " << st.st_size << " bytes\n";
//         std::cout << "Blocks: " << st.st_blocks << " (Block size: " << st.st_blksize << ")\n";

//         // Convert time_t to readable format
//         char timeStr[100];

//         std::cout << "Access time: ";
//         if (ctime_s(timeStr, sizeof(timeStr), &st.st_atim.tv_sec) == 0)
//             std::cout << timeStr << " (" << st.st_atim.tv_nsec << " ns)\n";

//         std::cout << "Modify time: ";
//         if (ctime_s(timeStr, sizeof(timeStr), &st.st_mtim.tv_sec) == 0)
//             std::cout << timeStr << " (" << st.st_mtim.tv_nsec << " ns)\n";

//         std::cout << "Change time: ";
//         if (ctime_s(timeStr, sizeof(timeStr), &st.st_ctim.tv_sec) == 0)
//             std::cout << timeStr << " (" << st.st_ctim.tv_nsec << " ns)\n";

//         std::cout << "Birth time: ";
//         if (ctime_s(timeStr, sizeof(timeStr), &st.st_birthtim.tv_sec) == 0)
//             std::cout << timeStr << " (" << st.st_birthtim.tv_nsec << " ns)\n";

//         std::cout << "Links: " << st.st_nlink << std::endl;
//     } else {
//         std::cerr << "Failed to get file information: " << GetLastError() << std::endl;
//         return 1;
//     }

//     return 0;
// }

#endif
