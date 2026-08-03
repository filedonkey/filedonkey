#if defined(_WIN32)

#include "fusebackend.h"
#include "pread_win32.h"
#include "posix_win32.h"
#include "pwrite_win32.h"
#include "lstat_win32.h"
#include "statvfs_win32.h"
#include "readlink_win32.h"

#include <QDebug>
#include <QRandomGenerator>

#include <stdlib.h>
#include <winfsp_fuse.h>
#include <errno.h>
#include <fileapi.h>
#include <errhandlingapi.h>
#include <handleapi.h>
#include <wchar.h>
#include <winnls.h>
#include <string>
#include <vector>

#define WIN_S_IFLNK  0120000  // symbolic link
#define WIN_S_IFDIR  0040000  // directory
#define WIN_S_IFREG  0100000  // regular file

#define WIN_S_IRUSR  0000400  // owner has read permission
#define WIN_S_IWUSR  0000200  // owner has write permission
#define WIN_S_IRGRP  0000040  // group has read permission
#define WIN_S_IWGRP  0000020  // group has write permission
#define WIN_S_IROTH  0000004  // others have read permission
#define WIN_S_IWOTH  0000002  // others have write permission

static std::wstring utf8ToWide(const std::string &utf8)
{
    if (utf8.empty()) return std::wstring();

    int wchars = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), NULL, 0);
    if (wchars <= 0) return std::wstring();

    std::wstring wide(wchars, L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), wide.data(), wchars) <= 0)
        return std::wstring();

    return wide;
}

static int openUtf8(const std::string &path, int flags, int mode)
{
    std::wstring wide = utf8ToWide(path);
    if (wide.empty())
    {
        errno = EINVAL;
        return -1;
    }

    return _wopen(wide.c_str(), flags, mode);
}

static std::string wideToUtf8(const wchar_t *wide)
{
    if (!wide || !*wide) return std::string();

    int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (bytes <= 1) return std::string();

    std::string utf8(bytes - 1, '\0'); // bytes includes the terminator
    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8.data(), bytes, NULL, NULL) <= 0)
        return std::string();

    return utf8;
}

Ref<ReaddirResult> FUSEBackend::FD_readdir(const char *path)
{
    std::string absolutePath = normalizePath(path);

    Ref<ReaddirResult> result = MakeRef<ReaddirResult>();

    // Enumerate with the wide API. The CRT's opendir/readdir speak the ANSI codepage, so any
    // name outside it came back mangled - an en dash arrived as the single byte 0x96 - and
    // then travelled the wire as invalid UTF-8. Names stay UTF-16 until the one
    // WideCharToMultiByte(CP_UTF8) below, which is the only place an encoding is chosen.
    std::wstring pattern = utf8ToWide(absolutePath);
    if (pattern.empty())
    {
        result->status = -EINVAL;
        return result;
    }

    if (pattern.back() != L'/' && pattern.back() != L'\\') pattern += L'\\';
    pattern += L'*';

    WIN32_FIND_DATAW findDataW;
    HANDLE handle = FindFirstFileW(pattern.c_str(), &findDataW);
    if (handle == INVALID_HANDLE_VALUE)
    {
        result->status = -ENOENT;
        return result;
    }

    std::vector<FindData> findDataList;

    do
    {
        if (findDataW.cFileName[0] == L'.') continue;
        if (findDataW.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;

        std::string name = wideToUtf8(findDataW.cFileName);

        // FindData::name is a fixed buffer, and one UTF-16 char can become four UTF-8 bytes,
        // so a name that does not fit is skipped rather than truncated mid sequence.
        if (name.empty() || name.size() >= sizeof(FindData::name)) continue;

        FindData &findData = findDataList.emplace_back();
        memset(&findData, 0, sizeof(FindData));

        bool isSymLink = (findDataW.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
                         findDataW.dwReserved0 == IO_REPARSE_TAG_SYMLINK;
        if (isSymLink)
        {
            findData.st_mode |= WIN_S_IFLNK;
        }
        else if (findDataW.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            findData.st_mode |= WIN_S_IFDIR;
        }
        else
        {
            findData.st_mode |= WIN_S_IFREG;
        }

        findData.st_mode |= WIN_S_IRUSR; // Owner can read
        findData.st_mode |= WIN_S_IWUSR; // Owner can write
        findData.st_mode |= WIN_S_IRGRP; // Group can read
        findData.st_mode |= WIN_S_IWGRP; // Group can write
        findData.st_mode |= WIN_S_IROTH; // Group can read
        findData.st_mode |= WIN_S_IWOTH; // Group can write

        findData.st_ino = QRandomGenerator::global()->generate64();
        memcpy(findData.name, name.c_str(), name.size());
    }
    while (FindNextFileW(handle, &findDataW));

    FindClose(handle);

    result->status = 0;
    result->count = findDataList.size();
    result->dataSize = sizeof(FindData) * result->count;
    result->findData = new FindData[result->count];
    memcpy(result->findData, findDataList.data(), result->dataSize);

    return result;
}

Ref<ReadResult> FUSEBackend::FD_read(cstr path, u64 size, i64 offset)
{
    auto absolutePath = normalizePath(path);

    Ref<ReadResult> result = MakeRef<ReadResult>(size);

    int fd = openUtf8(absolutePath, O_RDONLY, 0);
    if (fd == -1)
    {
        result->status = -errno;
        return result;
    }

    int res = win_pread_efficient(fd, result->data, size, offset);
    if (res == -1)
    {
        close(fd);
        result->status = -errno;
        return result;
    }

    close(fd);
    result->status = res;
    return result;
}

Ref<ReadlinkResult> FUSEBackend::FD_readlink(const char *path, u64 size)
{
    auto absolutePath = normalizePath(path);

    Ref<ReadlinkResult> result = MakeRef<ReadlinkResult>(size);

    int res = readlink(absolutePath.c_str(), result->data, size - 1);
    if (res == -1)
    {
        result->status = -errno;
        return result;
    }

    result->data[res] = '\0';

    return result;
}

Ref<StatfsResult> FUSEBackend::FD_statfs(const char *path)
{
    auto absolutePath = normalizePath(path);

    Ref<StatfsResult> result = MakeRef<StatfsResult>();

    struct fuse_statvfs stbuf;

    int res = statvfs(absolutePath.c_str(), &stbuf);
    if (res == -1)
    {
        result->status = -errno;
        return result;
    }

    result->f_bsize = stbuf.f_bsize;
    result->f_frsize = stbuf.f_frsize;
    result->f_blocks = stbuf.f_blocks;
    result->f_bfree = stbuf.f_bfree;
    result->f_bavail = stbuf.f_bavail;
    result->f_files = stbuf.f_files;
    result->f_ffree = stbuf.f_ffree;
    result->f_favail = stbuf.f_favail;
    result->f_fsid = stbuf.f_fsid;
    result->f_flag = stbuf.f_flag;
    result->f_namemax = stbuf.f_namemax;

    return result;
}

Ref<GetattrResult> FUSEBackend::FD_getattr(const char *path)
{
    auto absolutePath = normalizePath(path);

    Ref<GetattrResult> result = MakeRef<GetattrResult>();

    struct fuse_stat stbuf;

    int res = lstat(absolutePath.c_str(), &stbuf);
    if (res == -1)
    {
        result->status = -errno;
        return result;
    }

    result->st_dev = stbuf.st_dev;
    result->st_ino = stbuf.st_ino;
    result->st_nlink = stbuf.st_nlink;
    result->st_mode = stbuf.st_mode;
    result->st_uid = stbuf.st_uid;
    result->st_gid = stbuf.st_gid;
    result->st_rdev = stbuf.st_rdev;
    result->st_size = stbuf.st_size;
    result->st_blksize = stbuf.st_blksize;
    result->st_blocks = stbuf.st_blocks;
    result->st_atim.tv_sec = stbuf.st_atim.tv_sec;
    result->st_atim.tv_nsec = stbuf.st_atim.tv_nsec;
    result->st_mtim.tv_sec = stbuf.st_mtim.tv_sec;
    result->st_mtim.tv_nsec = stbuf.st_mtim.tv_nsec;
    result->st_ctim.tv_sec = stbuf.st_ctim.tv_sec;
    result->st_ctim.tv_nsec = stbuf.st_ctim.tv_nsec;

    qDebug() << "[FUSEBackend::FD_getattr] result->st_ino:" << result->st_ino;

    return result;
}

Ref<StatusResult> FUSEBackend::FD_write(const char *path, const char *buf, u64 size, i64 offset)
{
    auto absolutePath = normalizePath(path);

    Ref<StatusResult> result = MakeRef<StatusResult>();

    int fd = openUtf8(absolutePath, O_WRONLY, 0);
    if (fd == -1)
    {
        qDebug() << "[FUSEBackend::FD_write] can't open file to write";
        result->status = -errno;
        return result;
    }

    int res = pwrite_non_overlapped(fd, buf, size, offset);
    if (res == -1)
    {
        qDebug() << "[FUSEBackend::FD_write] can't write to file";
        result->status = -errno;
        close(fd);
        return result;
    }

    result->status = res;
    close(fd);
    return result;
}

Ref<StatusResult> FUSEBackend::FD_create(const char *path, u32 mode, i32 flags)
{
    auto absolutePath = normalizePath(path);

    (void)flags;

    Ref<StatusResult> result = MakeRef<StatusResult>();

    qDebug() << "[FUSEBackend::FD_create] flags:" << flags;
    qDebug() << "[FUSEBackend::FD_create] mode:" << mode;

    int fd = openUtf8(absolutePath, O_CREAT | O_WRONLY, mode);

    qDebug() << "[FUSEBackend::FD_create] fd:" << fd;

    if (fd == -1)
    {
        result->status = -errno;
        return result;
    }

    close(fd);
    return result;
}

Ref<StatusResult> FUSEBackend::FD_unlink(const char *path)
{
    auto absolutePath = normalizePath(path);

    Ref<StatusResult> result = MakeRef<StatusResult>();

    int res = unlink(absolutePath.c_str());
    if (res == -1)
    {
        result->status = -errno;
        return result;
    }

    return result;
}

Ref<StatusResult> FUSEBackend::FD_rename(const char *from, const char *to)
{
    auto absoluteOldPath = normalizePath(from);
    auto absoluteNewPath = normalizePath(to);

    Ref<StatusResult> result = MakeRef<StatusResult>();

    int res = rename(absoluteOldPath.c_str(), absoluteNewPath.c_str());
    if (res == -1)
    {
        result->status = -errno;
        return result;
    }

    return result;
}

Ref<StatusResult> FUSEBackend::FD_mkdir(const char *path, u32 mode)
{
    auto absolutePath = normalizePath(path);

    Ref<StatusResult> result = MakeRef<StatusResult>();

    qDebug() << "[FUSEBackend::FD_mkdir] mode:" << mode;

    int res = mkdir(absolutePath.c_str(), mode);
    if (res == -1)
    {
        result->status = -errno;
        return result;
    }

    return result;
}

Ref<StatusResult> FUSEBackend::FD_rmdir(const char *path)
{
    auto absolutePath = normalizePath(path);

    Ref<StatusResult> result = MakeRef<StatusResult>();

    int res = rmdir(absolutePath.c_str());
    if (res == -1)
    {
        result->status = -errno;
        return result;
    }

    return result;
}

Ref<StatusResult> FUSEBackend::FD_truncate(const char *path, i64 size)
{
    auto absolutePath = normalizePath(path);

    Ref<StatusResult> result = MakeRef<StatusResult>();

    int res = truncate(absolutePath.c_str(), size);
    if (res == -1)
    {
        result->status = -errno;
        return result;
    }

    return result;
}

#endif
