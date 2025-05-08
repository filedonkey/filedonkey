#ifndef FUSEBACKEND_H
#define FUSEBACKEND_H

#include <string.h>
#include <stdlib.h>

struct ReaddirResult
{
    int status;
    unsigned int count;
    unsigned int dataSize;
    void *findData;
};

static void ReadReaddirResult(ReaddirResult **result, const char *data)
{
    // ReaddirResult *result = (ReaddirResult *)malloc(sizeof(ReaddirResult));
    // memcpy(result, data, sizeof(ReaddirResult));
    // result->findData = (void *)malloc(result->dataSize);
    // memcpy(result->findData, data + sizeof(ReaddirResult), result->dataSize);
    // return result;
    *result = (ReaddirResult *)data;
    (*result)->findData = (void *)(data + sizeof(ReaddirResult));
}

static void FreeReaddirResult(ReaddirResult *result)
{
    free(result->findData);
    free(result);
}

class FUSEBackend
{
public:
    static ReaddirResult *FD_readdir(const char *path);
};

#endif // FUSEBACKEND_H
