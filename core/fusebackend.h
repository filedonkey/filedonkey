#ifndef FUSEBACKEND_H
#define FUSEBACKEND_H

#include "core.h"
#include "fusebackend_types.h"

#include <QString>
#include <QDir>
#include <QStorageInfo>

#include <filesystem>

class FUSEBackend
{
public:
    FUSEBackend() : publicDir(FUSEBackend::defualtPublicDir()) {};

    Ref<ReaddirResult>  FD_readdir(const char *path);
    Ref<ReadResult>     FD_read(const char *path, u64 size, i64 offset);
    Ref<WriteResult>    FD_write(const char *path, const char *buf, u64 size, i64 offset);
    Ref<ReadlinkResult> FD_readlink(const char *path, u64 size);
    Ref<StatfsResult>   FD_statfs(const char *path);
    Ref<GetattrResult>  FD_getattr(const char *path);
    Ref<CreateResult>   FD_create(const char *path, u32 mode, i32 flags);
    Ref<UnlinkResult>   FD_unlink(const char *path);

    std::filesystem::path normalizePath(const char *path)
    {
        return publicDir / std::filesystem::path(path).relative_path();
    }

    static std::filesystem::path defualtPublicDir()
    {
        // On Linux and MacOS this resolves to user folder.
        // On Windows: to first non system disk or to system
        // disk if it is the only one exists.

        QByteArray homePath = qgetenv("HOME");
        if (homePath.length()) return std::filesystem::path(homePath.toStdString());

        const QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();

        for (const QStorageInfo &storage : volumes) {
            if (!storage.isValid() || !storage.isReady())
                continue;

            if (volumes.length() > 1 && storage.isRoot())
                continue;

            return std::filesystem::path(storage.rootPath().toStdString());
        }

        return std::filesystem::path(QDir::homePath().toStdString());
    }

private:
    std::filesystem::path publicDir;
};

#endif // FUSEBACKEND_H
