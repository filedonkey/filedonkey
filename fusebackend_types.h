#ifndef FUSEBACKEND_TYPES_H
#define FUSEBACKEND_TYPES_H

#include "core.h"

#include <string.h>

struct FindData
{
    char name[1024];
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
        this->status = 0;
        this->size = 0;
        this->data = nullptr;
    }

    ReadResult(u64 size)
    {
        this->status = 0;
        this->size = size;
        this->data = new char[size];
        memset(this->data, 0, size);
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

struct ReadlinkResult
{
    i32 status;
    u64 size;
    char *data;

    ReadlinkResult()
    {
        this->status = 0;
        this->size = 0;
        this->data = nullptr;
    }

    ReadlinkResult(u64 size)
    {
        this->status = 0;
        this->size = size;
        this->data = new char[size];
        memset(this->data, 0, size);
    }

    ReadlinkResult(const char *data)
    {
        memcpy(this, data, sizeof(ReadlinkResult));
        this->data = new char[this->size]; // NOTE: this->size is NOT undefined here because of the memcpy call one line above.
        memcpy(this->data, data + sizeof(ReadlinkResult), this->size);
    }

    ~ReadlinkResult()
    {
        delete[] data;
    }
};

struct StatfsResult
{
    i32 status;
    // statvfs data
    u64 f_bsize;
    u64 f_frsize;
    u64 f_blocks;
    u64 f_bfree;
    u64 f_bavail;
    u64 f_files;
    u64 f_ffree;
    u64 f_favail;
    u64 f_fsid;
    u64 f_flag;
    u64 f_namemax;

    StatfsResult()
    {
        memset(this, 0, sizeof(StatfsResult));
    }

    StatfsResult(const char *data)
    {
        memcpy(this, data, sizeof(StatfsResult));
    }
};

struct fd_timespec {
  i64 tv_sec;
  i64 tv_nsec;
};

struct GetattrResult
{
    i32 status;
    // stat data
    u64 st_dev;
    u64 st_ino;
    u64 st_nlink;
    u32 st_mode;
    u32 st_uid;
    u32 st_gid;
    u64 st_rdev;
    i64 st_size;
    i64 st_blksize;
    i64 st_blocks;
    fd_timespec st_atim;
    fd_timespec st_mtim;
    fd_timespec st_ctim;

    GetattrResult()
    {
        memset(this, 0, sizeof(GetattrResult));
    }

    GetattrResult(const char *data)
    {
        memcpy(this, data, sizeof(GetattrResult));
    }
};

#endif // FUSEBACKEND_TYPES_H
