#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpServer>
#include <QUdpSocket>
#include <QTcpSocket>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void onBroadcasting();
    void onConnection();

private:
    void broadcast();

    Ui::MainWindow *ui = nullptr;
    QTcpServer     *server = nullptr;
    QUdpSocket     *broadcaster = nullptr;
    QTcpSocket     *socket = nullptr;
};
#endif // MAINWINDOW_H
