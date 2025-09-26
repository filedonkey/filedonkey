#include <QtTest>

// add necessary includes here

class FirstTest : public QObject
{
    Q_OBJECT

private slots:
    void test_case1() {
        QCOMPARE(true, true);
    }

    void test_case2() {
        QCOMPARE(true, true);
    }

    void test_case3() {
        QCOMPARE(true, true);
    }
};

// QTEST_APPLESS_MAIN(SecondTest)

// #include "tst_secondtest.moc"
