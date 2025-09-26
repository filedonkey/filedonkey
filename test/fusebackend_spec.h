#include "fusebackend.h"

#include <QtTest>
#include <QDir>
#include <QLoggingCategory>

class FUSEBackend_spec : public QObject
{
    Q_OBJECT

private slots:
    void Returns_correct_GetattrResult()
    {
        QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, false);

        QString appIconPath = QDir::currentPath() + "/../../../assets/filedonkey_app_icon.ico";
        Ref<GetattrResult> result = FUSEBackend::FD_getattr(appIconPath.toStdString().c_str());

        QCOMPARE(result->status, 0);
        QCOMPARE(result->st_dev, 0);
        QCOMPARE(result->st_size, 130121);
    }
};

// QTEST_APPLESS_MAIN(SecondTest)

// #include "tst_secondtest.moc"
