#ifndef FUSEBACKEND_H
#define FUSEBACKEND_H

#include "core.h"
#include "fusebackend_types.h"

class FUSEBackend
{
public:
    static Ref<ReaddirResult> FD_readdir(const char *path);
    static Ref<ReadResult>    FD_read(cstr path, u64 size, i64 offset);
};

#endif // FUSEBACKEND_H
