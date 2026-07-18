#ifdef linux

#include "fusebackend.h"

#include <vector>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

Ref<ReaddirResult> FUSEBackend::FD_readdir(const char *path)
{
    auto absolutePath = normalizePath(path);

    Ref<ReaddirResult> result = MakeRef<ReaddirResult>();

    std::vector<FindData> findDataList;
    DIR *dp;
    struct dirent *de;

    dp = opendir(absolutePath.string().c_str());
    if (dp == NULL)
    {
        result->status = -errno;
        return result;
    }

    while ((de = readdir(dp)) != NULL)
    {
        if (de->d_name[0] == '.') continue;

        FindData &findData = findDataList.emplace_back();
        memset(&findData, 0, sizeof(FindData));

        findData.st_ino = de->d_ino;
        findData.st_mode = de->d_type << 12;
        memcpy(findData.name, de->d_name, strlen(de->d_name));
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

    int fd = open(absolutePath.string().c_str(), O_RDONLY);
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

    int res = readlink(absolutePath.string().c_str(), result->data, size - 1);
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

    struct statvfs stbuf;

    int res = statvfs(absolutePath.string().c_str(), &stbuf);
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

    struct stat stbuf;

    int res = lstat(absolutePath.string().c_str(), &stbuf);
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
    result->st_atim.tv_sec = stbuf.st_atim.tv_sec;
    result->st_atim.tv_nsec = stbuf.st_atim.tv_nsec;
    result->st_mtim.tv_sec = stbuf.st_mtim.tv_sec;
    result->st_mtim.tv_nsec = stbuf.st_mtim.tv_nsec;
    result->st_ctim.tv_sec = stbuf.st_ctim.tv_sec;
    result->st_ctim.tv_nsec = stbuf.st_ctim.tv_nsec;

    return result;
}

Ref<WriteResult> FUSEBackend::FD_write(const char *path, const char *buf, u64 size, i64 offset)
{
    auto absolutePath = normalizePath(path);

    Ref<WriteResult> result = MakeRef<WriteResult>();

    int fd = open(absolutePath.string().c_str(), O_WRONLY);
    if (fd == -1)
    {
        result->status = -errno;
        return result;
    }

    int res = pwrite(fd, buf, size, offset);
    if (res == -1)
    {
        result->status = -errno;
        close(fd);
        return result;
    }

    result->status = res;
    close(fd);
    return result;
}

Ref<CreateResult> FUSEBackend::FD_create(const char *path, u32 mode, i32 flags)
{
    auto absolutePath = normalizePath(path);

    Ref<CreateResult> result = MakeRef<CreateResult>();

    qDebug() << "[FUSEBackend::FD_create] flags:" << flags;
    qDebug() << "[FUSEBackend::FD_create] mode:" << mode;

    int fd = open(absolutePath.string().c_str(), flags, mode);

    qDebug() << "[FUSEBackend::FD_create] fd:" << fd;

    if (fd == -1)
    {
        result->status = -errno;
        return result;
    }

    close(fd);
    return result;
}

Ref<UnlinkResult> FUSEBackend::FD_unlink(const char *path)
{
    auto absolutePath = normalizePath(path);

    Ref<UnlinkResult> result = MakeRef<UnlinkResult>();

    int res = unlink(absolutePath.string().c_str());
    if (res == -1)
    {
        result->status = -errno;
        return result;
    }

    return result;
}

#endif
