#include <QtTest>
#include "first_test.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    int result = 0;

    {
        FirstTest firstTest;
        result |= QTest::qExec(&firstTest, argc, argv);
    }
    // {
    //     SecondTest secondTest;
    //     result |= QTest::qExec(&secondTest, argc, argv);
    // }

    return result;
}
