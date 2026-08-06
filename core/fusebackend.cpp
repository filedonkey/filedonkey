// One backend for every platform, in the shape of WinFsp's own passthrough example
// (tst/passthrough-fuse3/passthrough-fuse3.c): the calls below are the real POSIX ones on Linux
// and macOS, and on Windows they come from posix_win32.h. Past the include block this file has
// no idea which it is talking to.

#include "fusebackend.h"

#include <errno.h>
#include <string.h>
#include <vector>

#if defined(_WIN32)
#include "posix_win32.h"
#else
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#endif

// The two structures lstat() and statvfs() fill have no name in common: they are native on Linux
// and macOS, and on Windows they are WinFsp's own, because Windows has nothing to be native to.
// One local name for each is what lets the bodies below stay identical.
#if defined(_WIN32)
typedef struct fuse_stat    fd_stat;
typedef struct fuse_statvfs fd_statvfs;
#else
typedef struct stat         fd_stat;
typedef struct statvfs      fd_statvfs;
#endif

#if defined(__APPLE__)
#include <sys/mount.h>
#else
// macOS spells the nanosecond stat fields st_*timespec; Linux and WinFsp spell them st_*tim.
#define st_atimespec st_atim
#define st_mtimespec st_mtim
#define st_ctimespec st_ctim
#endif

Ref<ReaddirResult> FUSEBackend::FD_readdir(const char *path)
{
    auto absolutePath = normalizePath(path);

    Ref<ReaddirResult> result = MakeRef<ReaddirResult>();

    DIR *dp = opendir(absolutePath.c_str());
    if (dp == NULL)
    {
        result->status = -errno;
        return result;
    }

    std::vector<FindData> findDataList;

    struct dirent *de;
    while ((de = readdir(dp)) != NULL)
    {
        if (de->d_name[0] == '.') continue;

        // FindData::name is a fixed buffer, and a name that does not fit is skipped rather than
        // truncated - half a name is a file the peer cannot open.
        size_t nameLength = strlen(de->d_name);
        if (nameLength >= sizeof(FindData::name)) continue;

        FindData &findData = findDataList.emplace_back();
        memset(&findData, 0, sizeof(FindData));

        findData.st_ino = de->d_ino;

        // DT_* shifted up is the matching S_IF* file type. The permission bits have to be here
        // too, and have to agree with FD_getattr: virtdisk's fd_readdir hands this mode to
        // filler() with FUSE_FILL_DIR_PLUS, so the client caches it as the file's real
        // attributes, and WinFsp and fuse-t both build their access checks out of it. A bare
        // file type is mode 000 - the peer sees a disk it may not read or write.
        findData.st_mode = (de->d_type << 12) | 0777;

        memcpy(findData.name, de->d_name, nameLength);
    }

    closedir(dp);

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

    int fd = open(absolutePath.c_str(), O_RDONLY);
    if (fd == -1)
    {
        result->status = -errno;
        return result;
    }

    int res = pread(fd, result->data, size, offset);
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

    fd_statvfs stbuf;

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

#if defined(__APPLE__)
    // statvfs on macOS reports f_bsize as the preferred I/O size rather than the allocation
    // block, which does not agree with the block counts above. statfs has the real one.
    struct statfs stfsbuf;
    if (statfs(absolutePath.c_str(), &stfsbuf) != -1) result->f_bsize = stfsbuf.f_bsize;
#endif

    return result;
}

Ref<GetattrResult> FUSEBackend::FD_getattr(const char *path)
{
    auto absolutePath = normalizePath(path);

    Ref<GetattrResult> result = MakeRef<GetattrResult>();

    fd_stat stbuf;

    int res = lstat(absolutePath.c_str(), &stbuf);
    if (res == -1)
    {
        result->status = -errno;
        return result;
    }

    result->st_dev = stbuf.st_dev;
    result->st_ino = stbuf.st_ino;
    result->st_nlink = stbuf.st_nlink;
    result->st_mode = stbuf.st_mode | 0777;
    result->st_uid = stbuf.st_uid;
    result->st_gid = stbuf.st_gid;
    result->st_rdev = stbuf.st_rdev;
    result->st_size = stbuf.st_size;
    result->st_blksize = stbuf.st_blksize;
    result->st_blocks = stbuf.st_blocks;
    result->st_atim.tv_sec = stbuf.st_atimespec.tv_sec;
    result->st_atim.tv_nsec = stbuf.st_atimespec.tv_nsec;
    result->st_mtim.tv_sec = stbuf.st_mtimespec.tv_sec;
    result->st_mtim.tv_nsec = stbuf.st_mtimespec.tv_nsec;
    result->st_ctim.tv_sec = stbuf.st_ctimespec.tv_sec;
    result->st_ctim.tv_nsec = stbuf.st_ctimespec.tv_nsec;

    return result;
}

Ref<StatusResult> FUSEBackend::FD_write(const char *path, const char *buf, u64 size, i64 offset)
{
    auto absolutePath = normalizePath(path);

    Ref<StatusResult> result = MakeRef<StatusResult>();

    int fd = open(absolutePath.c_str(), O_WRONLY);
    if (fd == -1)
    {
        result->status = -errno;
        return result;
    }

    int res = pwrite(fd, buf, size, offset);
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

Ref<StatusResult> FUSEBackend::FD_create(const char *path, u32 mode, i32 flags)
{
    auto absolutePath = normalizePath(path);

    // The flags are not usable: O_CREAT is 0x40 on Linux, 0x200 on macOS and 0x100 on Windows,
    // and this number was chosen by whichever peer asked. Read literally, a macOS peer's create
    // arrives on Linux as O_RDWR|O_TRUNC|O_NONBLOCK and quietly does not create anything. What
    // was meant is the same on every platform, so say it here instead.
    (void)flags;

    Ref<StatusResult> result = MakeRef<StatusResult>();

    // No O_TRUNC. A create that lands on a file which is already there must leave its contents
    // alone: an editor saving through us can ask to create the file it is saving, and emptying it
    // first means the save has destroyed the document before it writes a byte. This is what
    // master did on Windows too - _wopen(O_CREAT|O_WRONLY) is CreateFileW's OPEN_ALWAYS, which
    // does not truncate.
    //
    // The file type bits travel with the mode; open() wants only the permissions.
    int fd = open(absolutePath.c_str(), O_CREAT | O_WRONLY, mode & 0777);
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
