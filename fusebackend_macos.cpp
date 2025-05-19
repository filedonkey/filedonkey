#ifdef __APPLE__

#include "fusebackend.h"

#include <vector>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
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

#endif
