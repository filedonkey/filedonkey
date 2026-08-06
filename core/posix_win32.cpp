#if defined(_WIN32)

#include <windows.h>

#include "posix_win32.h"

#include <QRandomGenerator>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <string>

// Reparse point plumbing for readlink(). winioctl.h has these, but pulling it in for three
// definitions drags a great deal of unrelated device ioctl machinery along with it.
#ifndef IO_REPARSE_TAG_SYMLINK
#define IO_REPARSE_TAG_SYMLINK              (0xA000000CL)
#endif
#ifndef IO_REPARSE_TAG_MOUNT_POINT
#define IO_REPARSE_TAG_MOUNT_POINT          (0xA0000003L)
#endif
#ifndef FSCTL_GET_REPARSE_POINT
#define FSCTL_GET_REPARSE_POINT             CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 42, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif
#ifndef MAXIMUM_REPARSE_DATA_BUFFER_SIZE
#define MAXIMUM_REPARSE_DATA_BUFFER_SIZE    (16 * 1024)
#endif

typedef struct _REPARSE_DATA_BUFFER
{
    ULONG  ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    union
    {
        struct
        {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            ULONG  Flags;
            WCHAR  PathBuffer[1];
        } SymbolicLinkReparseBuffer;
        struct
        {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            WCHAR  PathBuffer[1];
        } MountPointReparseBuffer;
    };
} REPARSE_DATA_BUFFER;

// POSIX file type and permission bits. Windows has no header for these and we cannot use the
// CRT's _S_* - they stop at the two bits Windows models, and we report symlinks.
#define P_IFLNK     0120000
#define P_IFREG     0100000
#define P_IFDIR     0040000

#define P_IRWXU     0000700
#define P_IRWXG     0000070
#define P_IRWXO     0000007

// statvfs f_flag bits.
#define P_ST_RDONLY 0x00000001
#define P_ST_NOSUID 0x00000002

//----------------------------------------------------------------------------------------------
// Errors
//----------------------------------------------------------------------------------------------

static int win32_to_errno(DWORD winerrno)
{
    switch (winerrno)
    {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
    case ERROR_INVALID_NAME:
    case ERROR_BAD_PATHNAME:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_NET_NAME:
    case ERROR_FILENAME_EXCED_RANGE:
    case ERROR_NO_MORE_FILES:
        return ENOENT;

    case ERROR_ACCESS_DENIED:
    case ERROR_NETWORK_ACCESS_DENIED:
    case ERROR_CANNOT_MAKE:
    case ERROR_CURRENT_DIRECTORY:
        return EACCES;

    case ERROR_INVALID_HANDLE:
    case ERROR_INVALID_TARGET_HANDLE:
        return EBADF;

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
    case ERROR_HANDLE_DISK_FULL:
        return ENOSPC;

    case ERROR_TOO_MANY_OPEN_FILES:
        return EMFILE;

    case ERROR_OUTOFMEMORY:
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_NOT_ENOUGH_QUOTA:
        return ENOMEM;

    case ERROR_BROKEN_PIPE:
        return EPIPE;

    case ERROR_INVALID_USER_BUFFER:
        return EFAULT;

    case ERROR_NOT_A_REPARSE_POINT:
    case ERROR_INVALID_PARAMETER:
    case ERROR_INVALID_FUNCTION:
    case ERROR_NEGATIVE_SEEK:
        return EINVAL;

    default:
        return EIO;
    }
}

static int fail()
{
    errno = win32_to_errno(GetLastError());
    return -1;
}

static void *failNull()
{
    errno = win32_to_errno(GetLastError());
    return NULL;
}

//----------------------------------------------------------------------------------------------
// Paths
//----------------------------------------------------------------------------------------------

// Widen a UTF-8 path into the \\?\ form. The peer picks these paths, so "it fits in MAX_PATH"
// is not ours to assume, and \\?\ is the only way past that limit. Win32 hands a \\?\ path to
// the file system verbatim - nothing normalises it on the way - so this is also where '/'
// becomes '\' and runs of separators collapse. An empty return means the path was not UTF-8.
static std::wstring widenPath(const char *path)
{
    if (!path || !*path) return std::wstring();

    // A UNC path's own leading "\\" is exactly what the "UNC\" below stands in for.
    bool unc = (path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/');
    const char *rest = unc ? path + 2 : path;

    int wchars = MultiByteToWideChar(CP_UTF8, 0, rest, -1, NULL, 0); // counts the terminator
    if (wchars <= 0) return std::wstring();

    std::wstring wide(unc ? L"\\\\?\\UNC\\" : L"\\\\?\\");
    size_t head = wide.size();

    wide.resize(head + wchars - 1);
    if (MultiByteToWideChar(CP_UTF8, 0, rest, -1, wide.data() + head, wchars) <= 0)
        return std::wstring();

    // Separators in the tail only; the \\?\ head is already what it needs to be.
    wchar_t *dst = wide.data() + head;
    const wchar_t *src = dst;
    while (*src)
    {
        if (*src == L'/' || *src == L'\\')
        {
            while (src[1] == L'/' || src[1] == L'\\') ++src;
            *dst++ = L'\\';
            ++src;
        }
        else
        {
            *dst++ = *src++;
        }
    }
    wide.resize(dst - wide.data());

    // "D:" names the current directory on D:, which under \\?\ is nothing at all. "D:\" is the
    // root, which is what a caller reaching for a bare drive letter means.
    if (wide.size() == head + 2 && wide[head + 1] == L':') wide += L'\\';

    return wide;
}

static void unixtime(const FILETIME &ft, struct fuse_timespec &ts)
{
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;

    // FILETIME counts 100ns intervals from 1601-01-01; the Unix epoch is 11644473600s later.
    ts.tv_sec = (int64_t)(uli.QuadPart / 10000000ULL) - 11644473600LL;
    ts.tv_nsec = (int64_t)(uli.QuadPart % 10000000ULL) * 100;
}

//----------------------------------------------------------------------------------------------
// Files
//----------------------------------------------------------------------------------------------

// The mode argument POSIX allows after O_CREAT is accepted and ignored: Windows has no
// permission bits to put it in.
int open(const char *path, int oflag, ...)
{
    // O_RDONLY, O_WRONLY and O_RDWR are the values 0, 1 and 2 rather than independent bits, so
    // the access mode is a single value to test and not a set of flags to inspect.
    DWORD desiredAccess;
    switch (oflag & (O_RDONLY | O_WRONLY | O_RDWR))
    {
    case O_WRONLY: desiredAccess = GENERIC_WRITE;                break;
    case O_RDWR:   desiredAccess = GENERIC_READ | GENERIC_WRITE; break;
    default:       desiredAccess = GENERIC_READ;                 break;
    }

    // Appending is a right of its own on Windows. Granting it and withdrawing plain write leaves
    // the positioning to the file system, so an appender cannot overwrite what is already there.
    if (oflag & O_APPEND) desiredAccess = (desiredAccess & ~FILE_WRITE_DATA) | FILE_APPEND_DATA;

    // O_CREAT means "make it if it is missing", O_EXCL "and fail if it is not", O_TRUNC "empty
    // it". CreateFileW wants the three of them collapsed into one disposition.
    DWORD creationDisposition;
    if (oflag & O_CREAT)
    {
        if (oflag & O_EXCL)       creationDisposition = CREATE_NEW;
        else if (oflag & O_TRUNC) creationDisposition = CREATE_ALWAYS;
        else                      creationDisposition = OPEN_ALWAYS;
    }
    else
    {
        // POSIX ignores O_EXCL when O_CREAT is absent.
        creationDisposition = (oflag & O_TRUNC) ? TRUNCATE_EXISTING : OPEN_EXISTING;
    }

    std::wstring wide = widenPath(path);
    if (wide.empty())
    {
        errno = ENOENT;
        return -1;
    }

    HANDLE h = CreateFileW(wide.c_str(),
        desiredAccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        creationDisposition, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) return fail();

    return (int)(intptr_t)h;
}

int close(int fd)
{
    if (!CloseHandle((HANDLE)(intptr_t)fd)) return fail();

    return 0;
}

int pread(int fd, void *buf, size_t size, fuse_off_t offset)
{
    OVERLAPPED overlapped = {};
    overlapped.Offset = (DWORD)offset;
    overlapped.OffsetHigh = (DWORD)(offset >> 32);

    DWORD transferred = 0;
    if (!ReadFile((HANDLE)(intptr_t)fd, buf, (DWORD)size, &transferred, &overlapped))
    {
        // Reading from at or past the end is a short read of nothing, not a failure.
        if (GetLastError() == ERROR_HANDLE_EOF) return 0;
        return fail();
    }

    return (int)transferred;
}

int pwrite(int fd, const void *buf, size_t size, fuse_off_t offset)
{
    OVERLAPPED overlapped = {};
    overlapped.Offset = (DWORD)offset;
    overlapped.OffsetHigh = (DWORD)(offset >> 32);

    DWORD transferred = 0;
    if (!WriteFile((HANDLE)(intptr_t)fd, buf, (DWORD)size, &transferred, &overlapped))
        return fail();

    return (int)transferred;
}

//----------------------------------------------------------------------------------------------
// Metadata
//----------------------------------------------------------------------------------------------

// Deliberately lstat and not stat: we report symlinks as symlinks and serve readlink() for them,
// so this must describe the link itself. GetFileAttributesExW does that without opening the file.
int lstat(const char *path, struct fuse_stat *stbuf)
{
    if (!stbuf)
    {
        errno = EFAULT;
        return -1;
    }

    std::wstring wide = widenPath(path);
    if (wide.empty())
    {
        errno = ENOENT;
        return -1;
    }

    WIN32_FILE_ATTRIBUTE_DATA fileData;
    if (!GetFileAttributesExW(wide.c_str(), GetFileExInfoStandard, &fileData)) return fail();

    // A reparse point is not necessarily a symlink - dedup stubs and OneDrive placeholders are
    // reparse points over ordinary files - so the tag decides, and only FindFirstFileW reports it.
    bool isSymLink = false;
    if (fileData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
    {
        WIN32_FIND_DATAW findData;
        HANDLE h = FindFirstFileW(wide.c_str(), &findData);
        if (h != INVALID_HANDLE_VALUE)
        {
            isSymLink = findData.dwReserved0 == IO_REPARSE_TAG_SYMLINK;
            FindClose(h);
        }
    }

    ULARGE_INTEGER fileSize;
    fileSize.LowPart = fileData.nFileSizeLow;
    fileSize.HighPart = fileData.nFileSizeHigh;

    memset(stbuf, 0, sizeof *stbuf);

    if (isSymLink)
        stbuf->st_mode = P_IFLNK;
    else if (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        stbuf->st_mode = P_IFDIR;
    else
        stbuf->st_mode = P_IFREG;

    // Windows security does not map onto three permission triples, and the caller is the only
    // one who ever reaches these files, so say everything is permitted and let Windows itself
    // refuse the operations it wants to refuse.
    stbuf->st_mode |= P_IRWXU | P_IRWXG | P_IRWXO;

    stbuf->st_nlink = 1; // no cheap way to count hard links without opening the file
    stbuf->st_size = fileSize.QuadPart;

    unixtime(fileData.ftLastAccessTime, stbuf->st_atim);
    unixtime(fileData.ftLastWriteTime, stbuf->st_mtim);
    unixtime(fileData.ftLastWriteTime, stbuf->st_ctim);
    unixtime(fileData.ftCreationTime, stbuf->st_birthtim);

    // Windows exposes a file id only through an open handle, which is more than a getattr should
    // cost, so this is a number rather than an identity - nothing may key a cache on it.
    stbuf->st_ino = QRandomGenerator::global()->generate64();

    stbuf->st_blksize = 4096;
    stbuf->st_blocks = (fileSize.QuadPart + 511) / 512;
    stbuf->st_blocks += 1; // ext4 and APFS round the tail up to their own block; NTFS does not

    return 0;
}

int statvfs(const char *path, struct fuse_statvfs *stbuf)
{
    if (!stbuf)
    {
        errno = EFAULT;
        return -1;
    }

    std::wstring wide = widenPath(path);
    if (wide.empty())
    {
        errno = ENOENT;
        return -1;
    }

    // Not just the first two characters of the path: this finds the volume behind a mount point
    // or a UNC share too.
    WCHAR root[MAX_PATH + 1];
    if (!GetVolumePathNameW(wide.c_str(), root, MAX_PATH)) return fail();

    DWORD sectorsPerCluster, bytesPerSector, freeClusters, totalClusters;
    if (!GetDiskFreeSpaceW(root, &sectorsPerCluster, &bytesPerSector, &freeClusters, &totalClusters))
        return fail();

    // Only the cluster size is taken from the call above; the counts come from the Ex call, which
    // is the one Microsoft documents for large volumes and, more to the point, the only one that
    // separates free space from space this user may actually have. That is the f_bfree against
    // f_bavail distinction below, and a quota makes them differ.
    ULARGE_INTEGER availableBytes, totalBytes, freeBytes;
    if (!GetDiskFreeSpaceExW(root, &availableBytes, &totalBytes, &freeBytes)) return fail();

    DWORD volumeSerialNumber = 0, maximumComponentLength = 0, fileSystemFlags = 0;
    if (!GetVolumeInformationW(root, NULL, 0,
            &volumeSerialNumber, &maximumComponentLength, &fileSystemFlags, NULL, 0))
        return fail();

    memset(stbuf, 0, sizeof *stbuf);

    // Both are the cluster size: f_blocks below counts clusters, and POSIX reads those counts in
    // units of f_frsize, so a sector-sized f_frsize would report a volume eight times too small.
    uint64_t clusterSize = (uint64_t)bytesPerSector * sectorsPerCluster;
    stbuf->f_bsize = clusterSize;
    stbuf->f_frsize = clusterSize;

    stbuf->f_blocks = totalBytes.QuadPart / clusterSize;
    stbuf->f_bfree = freeBytes.QuadPart / clusterSize;
    stbuf->f_bavail = availableBytes.QuadPart / clusterSize;

    // Windows has no inode table to report on, so f_files, f_ffree and f_favail stay zero.

    stbuf->f_fsid = volumeSerialNumber;
    stbuf->f_namemax = maximumComponentLength;

    stbuf->f_flag = P_ST_NOSUID; // Windows has no set-user-ID bit to honour
    if (fileSystemFlags & FILE_READ_ONLY_VOLUME) stbuf->f_flag |= P_ST_RDONLY;

    return 0;
}

// POSIX semantics: no terminator is written, and a target longer than the buffer is truncated
// rather than reported.
int readlink(const char *path, char *buf, size_t size)
{
    if (size == 0)
    {
        errno = EINVAL;
        return -1;
    }

    std::wstring wide = widenPath(path);
    if (wide.empty())
    {
        errno = ENOENT;
        return -1;
    }

    // FILE_FLAG_OPEN_REPARSE_POINT so we open the link and not what it points at, and no access
    // bits at all because reading the reparse data needs none.
    HANDLE h = CreateFileW(wide.c_str(),
        0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h == INVALID_HANDLE_VALUE) return fail();

    REPARSE_DATA_BUFFER *reparse = (REPARSE_DATA_BUFFER *)malloc(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
    if (!reparse)
    {
        CloseHandle(h);
        errno = ENOMEM;
        return -1;
    }

    DWORD returned = 0;
    if (!DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, NULL, 0,
            reparse, MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &returned, NULL))
    {
        int res = fail(); // ERROR_NOT_A_REPARSE_POINT maps to EINVAL, which is what POSIX wants
        free(reparse);
        CloseHandle(h);
        return res;
    }

    CloseHandle(h);

    // The two tags we can follow lay their name offsets out identically; anything else is a
    // reparse point that is not a link.
    const WCHAR *pathBuffer;
    USHORT printOffset, printLength, substituteOffset, substituteLength;

    if (reparse->ReparseTag == IO_REPARSE_TAG_SYMLINK)
    {
        pathBuffer = reparse->SymbolicLinkReparseBuffer.PathBuffer;
        printOffset = reparse->SymbolicLinkReparseBuffer.PrintNameOffset;
        printLength = reparse->SymbolicLinkReparseBuffer.PrintNameLength;
        substituteOffset = reparse->SymbolicLinkReparseBuffer.SubstituteNameOffset;
        substituteLength = reparse->SymbolicLinkReparseBuffer.SubstituteNameLength;
    }
    else if (reparse->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT)
    {
        pathBuffer = reparse->MountPointReparseBuffer.PathBuffer;
        printOffset = reparse->MountPointReparseBuffer.PrintNameOffset;
        printLength = reparse->MountPointReparseBuffer.PrintNameLength;
        substituteOffset = reparse->MountPointReparseBuffer.SubstituteNameOffset;
        substituteLength = reparse->MountPointReparseBuffer.SubstituteNameLength;
    }
    else
    {
        free(reparse);
        errno = EINVAL;
        return -1;
    }

    // The print name is the readable one, but it is optional and links made by some tools carry
    // only a substitute name.
    const WCHAR *target = (const WCHAR *)((const BYTE *)pathBuffer + printOffset);
    int targetChars = printLength / sizeof(WCHAR);
    if (targetChars == 0)
    {
        target = (const WCHAR *)((const BYTE *)pathBuffer + substituteOffset);
        targetChars = substituteLength / sizeof(WCHAR);
    }

    int needed = targetChars > 0
        ? WideCharToMultiByte(CP_UTF8, 0, target, targetChars, NULL, 0, NULL, NULL)
        : 0;
    if (needed <= 0)
    {
        free(reparse);
        errno = targetChars > 0 ? EILSEQ : EINVAL;
        return -1;
    }

    int result;
    if ((size_t)needed <= size)
    {
        result = WideCharToMultiByte(CP_UTF8, 0, target, targetChars, buf, (int)size, NULL, NULL);
        if (result <= 0)
        {
            free(reparse);
            errno = EILSEQ;
            return -1;
        }
    }
    else
    {
        // WideCharToMultiByte will not fill a buffer it cannot fill completely, so convert
        // whole and then truncate, the way POSIX says a short buffer behaves.
        char *whole = (char *)malloc(needed);
        if (!whole)
        {
            free(reparse);
            errno = ENOMEM;
            return -1;
        }

        int converted = WideCharToMultiByte(CP_UTF8, 0, target, targetChars, whole, needed, NULL, NULL);
        if (converted > 0) memcpy(buf, whole, size);

        free(whole);

        if (converted <= 0)
        {
            free(reparse);
            errno = EILSEQ;
            return -1;
        }

        result = (int)size;
    }

    free(reparse);
    return result;
}

//----------------------------------------------------------------------------------------------
// Directory entries
//----------------------------------------------------------------------------------------------

int mkdir(const char *path, fuse_mode_t mode)
{
    (void)mode; // no permission bits to apply

    std::wstring wide = widenPath(path);
    if (wide.empty())
    {
        errno = ENOENT;
        return -1;
    }

    if (!CreateDirectoryW(wide.c_str(), NULL)) return fail();

    return 0;
}

int rmdir(const char *path)
{
    std::wstring wide = widenPath(path);
    if (wide.empty())
    {
        errno = ENOENT;
        return -1;
    }

    if (!RemoveDirectoryW(wide.c_str())) return fail();

    return 0;
}

int unlink(const char *path)
{
    std::wstring wide = widenPath(path);
    if (wide.empty())
    {
        errno = ENOENT;
        return -1;
    }

    if (!DeleteFileW(wide.c_str())) return fail();

    return 0;
}

int rename(const char *oldpath, const char *newpath)
{
    std::wstring wideOld = widenPath(oldpath);
    std::wstring wideNew = widenPath(newpath);
    if (wideOld.empty() || wideNew.empty())
    {
        errno = ENOENT;
        return -1;
    }

    if (!MoveFileExW(wideOld.c_str(), wideNew.c_str(), MOVEFILE_REPLACE_EXISTING)) return fail();

    return 0;
}

int truncate(const char *path, fuse_off_t size)
{
    std::wstring wide = widenPath(path);
    if (wide.empty())
    {
        errno = ENOENT;
        return -1;
    }

    HANDLE h = CreateFileW(wide.c_str(),
        FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) return fail();

    FILE_END_OF_FILE_INFO endOfFileInfo;
    endOfFileInfo.EndOfFile.QuadPart = size;

    BOOL ok = SetFileInformationByHandle(h, FileEndOfFileInfo, &endOfFileInfo, sizeof endOfFileInfo);
    if (!ok)
    {
        int res = fail();
        CloseHandle(h);
        return res;
    }

    CloseHandle(h);
    return 0;
}

//----------------------------------------------------------------------------------------------
// Directories
//----------------------------------------------------------------------------------------------

struct DirStream
{
    HANDLE handle;
    WIN32_FIND_DATAW findData;
    bool pending; // findData holds an entry FindFirstFileW already produced
    struct dirent de;
};

DIR *opendir(const char *path)
{
    std::wstring pattern = widenPath(path);
    if (pattern.empty())
    {
        errno = ENOENT;
        return NULL;
    }

    if (pattern.back() != L'\\') pattern += L'\\';
    pattern += L'*';

    DIR *dirp = (DIR *)calloc(1, sizeof *dirp);
    if (!dirp)
    {
        errno = ENOMEM;
        return NULL;
    }

    dirp->handle = FindFirstFileW(pattern.c_str(), &dirp->findData);
    if (dirp->handle == INVALID_HANDLE_VALUE)
    {
        free(dirp);
        return (DIR *)failNull();
    }

    dirp->pending = true;
    return dirp;
}

struct dirent *readdir(DIR *dirp)
{
    for (;;)
    {
        if (dirp->pending)
        {
            dirp->pending = false;
        }
        else if (!FindNextFileW(dirp->handle, &dirp->findData))
        {
            if (GetLastError() == ERROR_NO_MORE_FILES)
            {
                errno = 0;
                return NULL;
            }
            return (struct dirent *)failNull();
        }

        // Callers drop names beginning with a dot, which is how a POSIX file system says
        // "hidden". Windows says it with an attribute instead, so it is dropped here - a share
        // rooted at a drive letter is otherwise all $RECYCLE.BIN and System Volume Information.
        if (dirp->findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;

        int bytes = WideCharToMultiByte(CP_UTF8, 0, dirp->findData.cFileName, -1,
            dirp->de.d_name, FD_NAME_MAX, NULL, NULL);
        if (bytes <= 0) continue; // not representable, or longer than a name can be: skip it

        bool isSymLink = (dirp->findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                         dirp->findData.dwReserved0 == IO_REPARSE_TAG_SYMLINK;

        if (isSymLink)
            dirp->de.d_type = DT_LNK;
        else if (dirp->findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            dirp->de.d_type = DT_DIR;
        else
            dirp->de.d_type = DT_REG;

        // As in lstat(): a number, not an identity.
        dirp->de.d_ino = QRandomGenerator::global()->generate64();

        return &dirp->de;
    }
}

int closedir(DIR *dirp)
{
    FindClose(dirp->handle);
    free(dirp);

    return 0;
}

#endif // _WIN32
