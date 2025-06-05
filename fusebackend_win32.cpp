#define __FUSE__

#if defined(__WIN32)

#include "fusebackend.h"
#include "pread_win32.h"
#include "lstat_win32.h"
#include "statvfs_win32.h"
#include "readlink_win32.h"

#include <QDebug>
#include <QRandomGenerator>

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
        if (de->d_name[0] == '.') continue;

        size_t dientPathLen = strlen(dp->dd_name) + strlen(de->d_name);
        char *direntPath = (char *)alloca(dientPathLen);
        memset(direntPath, 0, dientPathLen);
        memcpy(direntPath, dp->dd_name, strlen(dp->dd_name));
        memcpy(direntPath + (strlen(dp->dd_name) - 1), de->d_name, strlen(de->d_name));

        int wchars_needed = MultiByteToWideChar(CP_UTF8, 0, direntPath, -1, NULL, 0);
        if (wchars_needed <= 0)
        {
            result->status = -1;
            return result;
        }

        wchar_t* wpath = new wchar_t[wchars_needed];
        if (MultiByteToWideChar(CP_UTF8, 0, direntPath, -1, wpath, wchars_needed) <= 0)
        {
            delete[] wpath;
            result->status = -1;
            return result;
        }

        WIN32_FILE_ATTRIBUTE_DATA fileData;
        BOOL success = GetFileAttributesExW(wpath, GetFileExInfoStandard, &fileData);

        if (fileData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;

        FindData &findData = findDataList.emplace_back();
        memset(&findData, 0, sizeof(FindData));

        if (success)
        {
#define WIN_S_IFLNK  0120000  // symbolic link
#define WIN_S_IFDIR  0040000  // directory
#define WIN_S_IFREG  0100000  // regular file

            bool isSymLink = (fileData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            if (isSymLink)
            {
                findData.st_mode |= WIN_S_IFLNK;
            }
            else if (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                findData.st_mode |= WIN_S_IFDIR;
            }
            else
            {
                findData.st_mode |= WIN_S_IFREG;
            }
        }

        qDebug() << "[FD_readdir] dd_name" << dp->dd_name << strlen(dp->dd_name);
        qDebug() << "[FD_readdir] d_name" << de->d_name << strlen(de->d_name);
        qDebug() << "[FD_readdir] direntPath" << direntPath << strlen(direntPath);

        findData.st_ino = QRandomGenerator::global()->generate64(); // de->d_ino;
        // findData.stat.st_mode = st_mode; // de->d_type << 12;
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
    char *buf = new char[size];

    int fd = open(path, O_RDONLY);
    if (fd == -1)
    {
        Ref<ReadResult> result = MakeRef<ReadResult>();
        result->status = -errno;
        return result;
    }

    int res = pread(fd, buf, size, offset);
    if (res == -1)
    {
        close(fd);
        Ref<ReadResult> result = MakeRef<ReadResult>();
        result->status = -errno;
        return result;
    }
    close(fd);

    Ref<ReadResult> result = MakeRef<ReadResult>(res);
    memcpy(result->data, buf, res);
    result->status = res;

    delete[] buf;

    return result;
}

Ref<ReadlinkResult> FUSEBackend::FD_readlink(const char *path, u64 size)
{
    Ref<ReadlinkResult> result = MakeRef<ReadlinkResult>(size);

    int res = readlink(path, result->data, size - 1);
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
    Ref<StatfsResult> result = MakeRef<StatfsResult>();

    struct statvfs stbuf;

    int res = statvfs(path, &stbuf);
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
    Ref<GetattrResult> result = MakeRef<GetattrResult>();

    struct FUSE_STAT stbuf;

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
    result->st_atim.tv_sec = stbuf.st_atim.tv_sec;
    result->st_atim.tv_nsec = stbuf.st_atim.tv_nsec;
    result->st_mtim.tv_sec = stbuf.st_mtim.tv_sec;
    result->st_mtim.tv_nsec = stbuf.st_mtim.tv_nsec;
    result->st_ctim.tv_sec = stbuf.st_ctim.tv_sec;
    result->st_ctim.tv_nsec = stbuf.st_ctim.tv_nsec;

    qDebug() << "[FUSEBackend::FD_getattr] result->st_ino:" << result->st_ino;

    return result;
}

#endif
