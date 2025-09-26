#include <QtTest>
#include "fusebackend_spec.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    int result = 0;

    {
        FUSEBackend_spec spec;
        result |= QTest::qExec(&spec, argc, argv);
    }
    // {
    //     SecondTest secondTest;
    //     result |= QTest::qExec(&secondTest, argc, argv);
    // }

    return result;
}
