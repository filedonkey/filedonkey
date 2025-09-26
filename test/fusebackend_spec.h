#include "fusebackend.h"

#include <QtTest>
#include <QDir>
#include <QLoggingCategory>
#include <QFile>

class FUSEBackend_spec : public QObject
{
    Q_OBJECT

private slots:
    void Returns_correct_GetattrResult()
    {
        QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, false);

        QString appIconPath = QDir::currentPath() + "/assets/filedonkey_app_icon.ico";
        Ref<GetattrResult> result = FUSEBackend::FD_getattr(appIconPath.toStdString().c_str());

        QFile appIcon = QFile(appIconPath);
        QCOMPARE(appIcon.exists(), true);

        QCOMPARE(result->status, 0);
//        QCOMPARE(result->st_dev, 0);
        QCOMPARE(result->st_ino, 0);
        QCOMPARE(result->st_nlink, 0);
        QCOMPARE(result->st_mode, 0);
        QCOMPARE(result->st_uid, 0);
        QCOMPARE(result->st_gid, 0);
        QCOMPARE(result->st_rdev, 0);
        QCOMPARE(result->st_size, 130121);
        QCOMPARE(result->st_blksize, 0);
        QCOMPARE(result->st_blocks, 0);
        QCOMPARE(result->st_atim.tv_sec, 0);
        QCOMPARE(result->st_atim.tv_nsec, 0);
        QCOMPARE(result->st_mtim.tv_sec, 0);
        QCOMPARE(result->st_mtim.tv_nsec, 0);
        QCOMPARE(result->st_ctim.tv_sec, 0);
        QCOMPARE(result->st_ctim.tv_nsec, 0);
    }
};
