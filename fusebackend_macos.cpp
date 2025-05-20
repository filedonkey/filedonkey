#ifdef __APPLE__

#include "fusebackend.h"

#include <vector>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

Ref<ReaddirResult> FUSEBackend::FD_readdir(const char *path)
{
    Ref<ReaddirResult> result = MakeRef<ReaddirResult>();

    std::vector<FindData> findDataList;
    DIR *dp;
    struct dirent *de;

    dp = opendir(path);
    if (dp == NULL)
    {
        result->status = -errno;
        return result;
    }

    while ((de = readdir(dp)) != NULL)
    {
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
    Ref<ReadResult> result = MakeRef<ReadResult>(size);

    int fd = open(path, O_RDONLY);
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
    return result;
}

Ref<StatfsResult> FUSEBackend::FD_statfs(const char *path)
{
    Ref<StatfsResult> result = MakeRef<StatfsResult>();

    struct statvfs stbuf;

    int res = statvfs(path, &stbuf);
    if (res == -1)
    {
        result->status = -errno;
        return result;
    }

    memcpy(result.get() + sizeof(result->status), &stbuf, sizeof(stbuf));

    return result;
}

Ref<GetattrResult> FUSEBackend::FD_getattr(const char *path)
{
    Ref<GetattrResult> result = MakeRef<GetattrResult>();

    struct stat stbuf;

    int res = lstat(path, &stbuf);
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
    result->st_atim.tv_sec = stbuf.st_atimespec.tv_sec;
    result->st_atim.tv_nsec = stbuf.st_atimespec.tv_nsec;
    result->st_mtim.tv_sec = stbuf.st_mtimespec.tv_sec;
    result->st_mtim.tv_nsec = stbuf.st_mtimespec.tv_nsec;
    result->st_ctim.tv_sec = stbuf.st_ctimespec.tv_sec;
    result->st_ctim.tv_nsec = stbuf.st_ctimespec.tv_nsec;

    return result;
}

#endif
