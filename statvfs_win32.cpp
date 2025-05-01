#if defined(__WIN32)

#include <windows.h>
#include <fuse.h>
#include <iostream>
#include <string>

// Windows specific flags that can be mapped to statvfs flags
#define WIN_ST_RDONLY      0x00000001  // Read-only file system
#define WIN_ST_NOSUID      0x00000002  // No support for ST_ISUID and ST_ISGID file mode bits

// Function to get statvfs-like information on Windows
int statvfs(const char* path, struct statvfs* buf) {
    if (!path || !buf) {
        return -1;  // Invalid parameters
    }

    // Get drive information
    ULARGE_INTEGER freeBytesAvailable;
    ULARGE_INTEGER totalNumberOfBytes;
    ULARGE_INTEGER totalNumberOfFreeBytes;

    // Need to convert path to root path format (e.g., "C:\")
    std::string rootPath;
    if (path[0] && path[1] == ':') {
        rootPath = std::string(1, path[0]) + ":\\";
    } else {
        // Use current drive if not specified
        char currentDir[MAX_PATH];
        if (GetCurrentDirectoryA(MAX_PATH, currentDir)) {
            rootPath = std::string(1, currentDir[0]) + ":\\";
        } else {
            return -1;
        }
    }

    if (!GetDiskFreeSpaceExA(rootPath.c_str(),
                             &freeBytesAvailable,
                             &totalNumberOfBytes,
                             &totalNumberOfFreeBytes)) {
        std::cerr << "GetDiskFreeSpaceEx failed with error: " << GetLastError() << std::endl;
        return -1;
    }

    // Get volume information
    char volumeName[MAX_PATH + 1] = { 0 };
    DWORD volumeSerialNumber = 0;
    DWORD maximumComponentLength = 0;
    DWORD fileSystemFlags = 0;
    char fileSystemNameBuffer[MAX_PATH + 1] = { 0 };

    if (!GetVolumeInformationA(rootPath.c_str(),
                               volumeName, MAX_PATH,
                               &volumeSerialNumber,
                               &maximumComponentLength,
                               &fileSystemFlags,
                               fileSystemNameBuffer, MAX_PATH)) {
        std::cerr << "GetVolumeInformation failed with error: " << GetLastError() << std::endl;
        return -1;
    }

    // Get allocation information
    DWORD sectorsPerCluster;
    DWORD bytesPerSector;
    DWORD numberOfFreeClusters;
    DWORD totalNumberOfClusters;

    if (!GetDiskFreeSpaceA(rootPath.c_str(),
                           &sectorsPerCluster,
                           &bytesPerSector,
                           &numberOfFreeClusters,
                           &totalNumberOfClusters)) {
        std::cerr << "GetDiskFreeSpace failed with error: " << GetLastError() << std::endl;
        return -1;
    }

    // Fill the statvfs-like structure
    buf->f_bsize = bytesPerSector * sectorsPerCluster;  // Block size (cluster size)
    buf->f_frsize = bytesPerSector;                     // Fragment size (sector size)

    buf->f_blocks = totalNumberOfBytes.QuadPart / buf->f_bsize;
    buf->f_bfree = totalNumberOfFreeBytes.QuadPart / buf->f_bsize;
    buf->f_bavail = freeBytesAvailable.QuadPart / buf->f_bsize;

    // Windows doesn't provide inode information directly
    buf->f_files = 0;   // Not available on Windows
    buf->f_ffree = 0;   // Not available on Windows
    buf->f_favail = 0;  // Not available on Windows

    buf->f_fsid = volumeSerialNumber;
    buf->f_namemax = maximumComponentLength;

    // Map Windows file system flags to statvfs flags
    buf->f_flag = 0;
    if (fileSystemFlags & FILE_READ_ONLY_VOLUME) {
        buf->f_flag |= WIN_ST_RDONLY;
    }
    // Windows doesn't support SUID/SGID
    buf->f_flag |= WIN_ST_NOSUID;

    return 0;
}

// Example usage
// int main(int argc, char** argv) {
//     const char* path = argc > 1 ? argv[1] : "C:";
//     struct win_statvfs sv;

//     if (win_statvfs(path, &sv) == 0) {
//         std::cout << "Filesystem information for " << path << ":\n";
//         std::cout << "Block size: " << sv.f_bsize << " bytes\n";
//         std::cout << "Fragment size: " << sv.f_frsize << " bytes\n";
//         std::cout << "Total blocks: " << sv.f_blocks << "\n";
//         std::cout << "Free blocks: " << sv.f_bfree << "\n";
//         std::cout << "Available blocks: " << sv.f_bavail << "\n";
//         std::cout << "Total space: " << (sv.f_blocks * sv.f_frsize / (1024.0 * 1024.0 * 1024.0)) << " GB\n";
//         std::cout << "Free space: " << (sv.f_bfree * sv.f_frsize / (1024.0 * 1024.0 * 1024.0)) << " GB\n";
//         std::cout << "Available space: " << (sv.f_bavail * sv.f_frsize / (1024.0 * 1024.0 * 1024.0)) << " GB\n";
//         std::cout << "Max filename length: " << sv.f_namemax << " characters\n";
//         std::cout << "Read-only: " << ((sv.f_flag & WIN_ST_RDONLY) ? "Yes" : "No") << "\n";
//     } else {
//         std::cerr << "Failed to get filesystem information\n";
//         return 1;
//     }

//     return 0;
// }

#endif
