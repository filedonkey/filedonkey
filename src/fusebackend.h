#ifndef FUSEBACKEND_H
#define FUSEBACKEND_H

#include "core.h"
#include "fusebackend_types.h"

class FUSEBackend
{
public:
    static Ref<ReaddirResult>  FD_readdir(const char *path);
    static Ref<ReadResult>     FD_read(const char *path, u64 size, i64 offset);
    static i32                 FD_write(const char *path, const char *buf, u64 size, i64 offset);
    static Ref<ReadlinkResult> FD_readlink(const char *path, u64 size);
    static Ref<StatfsResult>   FD_statfs(const char *path);
    static Ref<GetattrResult>  FD_getattr(const char *path);
};

#endif // FUSEBACKEND_H
