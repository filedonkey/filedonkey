#ifdef __APPLE__

#include "fusebackend.h"

ReaddirResult *FUSEBackend::FD_readdir(const char *path)
{
    ReaddirResult *result = (ReaddirResult *)malloc(sizeof(ReaddirResult));
    memset(result, 0, sizeof(ReaddirResult));

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
    result->findData = (FindData *)malloc(result->dataSize);
    memcpy(result->findData, findDataList.data(), result->dataSize);

    return result;
}

#endif
