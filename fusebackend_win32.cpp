#define __FUSE__

#if defined(__WIN32) && defined(__FUSE__)

#include "fusebackend.h"

#include <QDebug>

#include <stdlib.h>
#include <fuse/fuse_win.h>
#include <stdlib.h>
#include <dirent.h>
#include <fileapi.h>
#include <errhandlingapi.h>
#include <handleapi.h>
#include <wchar.h>
#include <winnls.h>
#include <vector>

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

    while ((de = readdir(dp)) != NULL) {
        FindData &findData = findDataList.emplace_back();
        memset(&findData, 0, sizeof(FindData));

        size_t dientPathLen = strlen(dp->dd_name) + strlen(de->d_name);
        char *direntPath = (char *)alloca(dientPathLen);
        memset(direntPath, 0, dientPathLen);
        memcpy(direntPath, dp->dd_name, strlen(dp->dd_name));
        memcpy(direntPath + (strlen(dp->dd_name) - 1), de->d_name, strlen(de->d_name));

        int wchars_needed = MultiByteToWideChar(CP_UTF8, 0, direntPath, -1, NULL, 0);
        if (wchars_needed <= 0) {
            result->status = -1;
            return result;
        }

        wchar_t* wpath = new wchar_t[wchars_needed];
        if (MultiByteToWideChar(CP_UTF8, 0, direntPath, -1, wpath, wchars_needed) <= 0) {
            delete[] wpath;
            result->status = -1;
            return result;
        }

        WIN32_FILE_ATTRIBUTE_DATA fileData;
        BOOL success = GetFileAttributesExW(wpath, GetFileExInfoStandard, &fileData);
        findData.st_mode = 0;
        if (success)
        {
#define WIN_S_IFLNK  0120000  // symbolic link
#define WIN_S_IFDIR  0040000  // directory
#define WIN_S_IFREG  0100000  // regular file

            bool isSymLink = (fileData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            if (isSymLink) {
                findData.st_mode |= WIN_S_IFLNK;
            } else if (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                findData.st_mode |= WIN_S_IFDIR;
            } else {
                findData.st_mode |= WIN_S_IFREG;
            }
        }

        qDebug() << "[FD_readdir] dd_name" << dp->dd_name << strlen(dp->dd_name);
        qDebug() << "[FD_readdir] d_name" << de->d_name << strlen(de->d_name);
        qDebug() << "[FD_readdir] direntPath" << direntPath << strlen(direntPath);

        findData.st_ino = de->d_ino;
        // findData.stat.st_mode = st_mode; // de->d_type << 12;
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
