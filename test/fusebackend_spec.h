#include "fusebackend.h"

#include <QtTest>
#include <QDir>
#include <QLoggingCategory>
#include <QFile>
#include <QStorageInfo>

class FUSEBackend_spec : public QObject
{
    Q_OBJECT

private:
    QString appIconPath = QDir::currentPath() + "/assets/filedonkey_app_icon.ico";

private slots:
    void Returns_correct_GetattrResult()
    {
        QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, false);

        Ref<GetattrResult> result = FUSEBackend::FD_getattr(appIconPath.toStdString().c_str());

        QFile appIcon = QFile(appIconPath);
        qint64 fileAccessTime = appIcon.fileTime(QFileDevice::FileAccessTime).toSecsSinceEpoch();
        qint64 fileModificationTime = appIcon.fileTime(QFileDevice::FileModificationTime).toSecsSinceEpoch();
        qint64 fileStatusChangeTime = appIcon.fileTime(QFileDevice::FileMetadataChangeTime).toSecsSinceEpoch();

        QCOMPARE(appIcon.exists(), true);

        QCOMPARE(result->status, 0);
        QCOMPARE(result->st_nlink, 1);
        QCOMPARE(result->st_mode, 33188);
        QCOMPARE(result->st_rdev, 0);
        QCOMPARE(result->st_size, 130121);
        QCOMPARE(result->st_blksize, 4096);
        QCOMPARE(result->st_blocks, 256);
        QCOMPARE(result->st_atim.tv_sec, fileAccessTime);
        QCOMPARE(result->st_mtim.tv_sec, fileModificationTime);
        QCOMPARE(result->st_ctim.tv_sec, fileStatusChangeTime);

        // Platfrom specific
        // QCOMPARE(result->st_dev, 0);
        // QCOMPARE(result->st_ino, 0);
        // QCOMPARE(result->st_uid, 0);
        // QCOMPARE(result->st_gid, 0);

        // Qt does not support ns, only ms
        // QCOMPARE(result->st_atim.tv_nsec, 0);
        // QCOMPARE(result->st_mtim.tv_nsec, 0);
        // QCOMPARE(result->st_ctim.tv_nsec, 0);
    }

    void Returns_correct_ReadResult()
    {
        const u32 BLOCK_SIZE = 65535;
        Ref<ReadResult> result = FUSEBackend::FD_read(appIconPath.toStdString().c_str(), BLOCK_SIZE, 0);

        QFile appIcon = QFile(appIconPath);
        appIcon.open(QIODeviceBase::ReadOnly);
        char fileData[BLOCK_SIZE];
        appIcon.read(fileData, BLOCK_SIZE);
        appIcon.close();

        QCOMPARE(result->status, BLOCK_SIZE);
        QCOMPARE(result->size, BLOCK_SIZE);
        QCOMPARE(memcmp(result->data, fileData, BLOCK_SIZE), 0);
    }

    void Returns_correct_StatfsResult()
    {
        Ref<StatfsResult> result = FUSEBackend::FD_statfs(QDir::currentPath().toStdString().c_str());
        QStorageInfo storageInfo(QDir::currentPath());

        QCOMPARE(result->status, 0);
        QCOMPARE(result->f_bsize, storageInfo.blockSize());

        QCOMPARE(result->f_blocks, storageInfo.bytesTotal() / storageInfo.blockSize());
        QCOMPARE(result->f_bfree, storageInfo.bytesFree() / storageInfo.blockSize());
        QCOMPARE(result->f_bavail, storageInfo.bytesAvailable() / storageInfo.blockSize());
        QCOMPARE(result->f_namemax, 255);

        // Not available on Windows
        // QCOMPARE(result->f_files, 0);
        // QCOMPARE(result->f_ffree, 0);
        // QCOMPARE(result->f_favail, 0);

        // Different on each machine
        // QCOMPARE(result->f_frsize, 4096);
        // QCOMPARE(result->f_fsid, 0);
        // QCOMPARE(result->f_flag, 0);
    }

    void Returns_correct_ReaddirResult()
    {
        QString assetsPath = QDir::currentPath() + "/assets/";
        Ref<ReaddirResult> result = FUSEBackend::FD_readdir(assetsPath.toStdString().c_str());
        FindData *fd = (FindData *)result->findData;

        QDir dir(assetsPath);
        static QRegularExpression re("[^\\.]");
        auto files = dir.entryList().filter(re); // We don't care about '.' and '..' files

        QCOMPARE(files.size(), 3);
        QCOMPARE(files.contains("filedonkey_app_icon.ico"), true);
        QCOMPARE(files.contains("filedonkey_tray_icon_dark.ico"), true);
        QCOMPARE(files.contains("filedonkey_tray_icon_light.ico"), true);

        QCOMPARE(result->status, 0);
        QCOMPARE(result->count, 3);
        QCOMPARE(result->dataSize, sizeof(FindData) * 3);

        QCOMPARE(files.contains((fd + 0)->name), true);
        QCOMPARE(files.contains((fd + 1)->name), true);
        QCOMPARE(files.contains((fd + 2)->name), true);

        QCOMPARE((fd + 0)->st_mode, 32768);
        QCOMPARE((fd + 1)->st_mode, 32768);
        QCOMPARE((fd + 2)->st_mode, 32768);
    }
};
