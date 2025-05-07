#ifndef FUSEFILESYSTEM_H
#define FUSEFILESYSTEM_H

struct ReaddirResult
{
    int status;
    void *findData;
    unsigned int dataSize;
    unsigned int count;
};

class FUSEFileSystem
{
public:
    static ReaddirResult *FD_readdir(const char *path, void *fi);
};

#endif // FUSEFILESYSTEM_H
