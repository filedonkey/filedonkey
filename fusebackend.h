#ifndef FUSEBACKEND_H
#define FUSEBACKEND_H

#include "core.h"

#include <string.h>
#include <stdlib.h>

#include <QDebug>

struct FindData
{
    i8 name[1024];
    u64 st_ino;
    u16 st_mode;
};

struct ReaddirResult
{
    i32 status;
    u32 count;
    u32 dataSize;
    FindData *findData;

    ReaddirResult()
    {
        memset(this, 0, sizeof(ReaddirResult));
    }

    ReaddirResult(const char *data)
    {
        memcpy(this, data, sizeof(ReaddirResult));
        this->findData = new FindData[this->dataSize / sizeof(FindData)];
        memcpy(this->findData, data + sizeof(ReaddirResult), this->dataSize);
    }

    ~ReaddirResult()
    {
        qDebug() << "[ReaddirResult] destructor";
        delete[] this->findData;
    }
};

struct ReadResult
{
    i32 status;
    u64 size;
    char *data;

    ReadResult()
    {
        status = 0;
        data = nullptr;
    }

    ReadResult(u64 size)
    {
        status = 0;
        data = new char[size];
        memset(data, 0, size);
    }

    ReadResult(const char *data)
    {
        memcpy(this, data, sizeof(ReadResult));
        this->data = new char[this->size]; // NOTE: this->size is NOT undefined here because of the memcpy call one line above.
        memcpy(this->data, data + sizeof(ReadResult), this->size);
    }

    ~ReadResult()
    {
        delete[] data;
    }
};

class FUSEBackend
{
public:
    static Ref<ReaddirResult> FD_readdir(const char *path);
    static Ref<ReadResult>    FD_read(cstr path, u64 size, i64 offset);
};

#endif // FUSEBACKEND_H
