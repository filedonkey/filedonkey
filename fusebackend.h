#ifndef FUSEBACKEND_H
#define FUSEBACKEND_H

#include "core.h"

#include <string.h>
#include <stdlib.h>

#include <QDebug>

struct FindData
{
    char name[1024];
    unsigned long long st_ino;
    unsigned short st_mode;
};

struct ReaddirResult
{
    int status;
    unsigned int count;
    unsigned int dataSize;
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

class FUSEBackend
{
public:
    static Ref<ReaddirResult> FD_readdir(const char *path);
};

#endif // FUSEBACKEND_H
