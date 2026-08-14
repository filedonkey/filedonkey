#ifndef FUSEBACKEND_H
#define FUSEBACKEND_H

#include "core.h"
#include "fusebackend_types.h"

#include <QString>
#include <QDir>
#include <QStorageInfo>

#include <cctype>

class FUSEBackend
{
public:
    // The directory this machine serves, and everything under it. Taken as it is given - an empty
    // one falls back to the default, so a caller with nothing stored need not ask for it itself.
    //
    // Read once, here, and never changed afterwards: normalizePath() runs on the handler pool's
    // threads, several at a time, and a directory that moved under them would be a data race on
    // every request. A folder chosen in the settings is served from the next start, which is also
    // the only sane answer for the peers holding a mount of the old one.
    explicit FUSEBackend(const std::string &dir = std::string())
        : publicDir(dir.empty() ? FUSEBackend::defualtPublicDir() : dir) {};

    Ref<ReaddirResult>  FD_readdir(const char *path);
    Ref<ReadResult>     FD_read(const char *path, u64 size, i64 offset);
    Ref<StatusResult>   FD_write(const char *path, const char *buf, u64 size, i64 offset);
    Ref<ReadlinkResult> FD_readlink(const char *path, u64 size);
    Ref<StatfsResult>   FD_statfs(const char *path);
    Ref<GetattrResult>  FD_getattr(const char *path);
    Ref<StatusResult>   FD_create(const char *path, u32 mode, i32 flags);
    Ref<StatusResult>   FD_unlink(const char *path);
    Ref<StatusResult>   FD_rename(const char *from, const char *to);
    Ref<StatusResult>   FD_mkdir(const char *path, u32 mode);
    Ref<StatusResult>   FD_rmdir(const char *path);
    Ref<StatusResult>   FD_truncate(const char *path, i64 size);

    std::string normalizePath(const char *path)
    {
        std::string relative = path ? path : "";

        // Drop a Windows root name ("D:").
        if (relative.size() >= 2 && relative[1] == ':' && isalpha((unsigned char)relative[0]))
            relative.erase(0, 2);

        size_t start = relative.find_first_not_of("/\\");
        relative = (start == std::string::npos) ? std::string() : relative.substr(start);

        std::string result = publicDir;
        if (!result.empty() && result.back() != '/' && result.back() != '\\' && !relative.empty())
            result += '/';

        return result + relative;
    }

    static std::string defualtPublicDir()
    {
        // On Linux and MacOS this resolves to user folder.
        // On Windows: to first non system disk or to system
        // disk if it is the only one exists.

        QByteArray homePath = qgetenv("HOME");
        if (homePath.length()) return homePath.toStdString();

        const QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();

        for (const QStorageInfo &storage : volumes) {
            if (!storage.isValid() || !storage.isReady())
                continue;

            if (volumes.length() > 1 && storage.isRoot())
                continue;

            return storage.rootPath().toStdString();
        }

        return QDir::homePath().toStdString();
    }

private:
    std::string publicDir;
};

#endif // FUSEBACKEND_H
