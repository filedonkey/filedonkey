#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QTcpServer>
#include <QUdpSocket>
#include <QTcpSocket>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

struct Connection
{
    QString machineId;
    QString machineName;
    QString machineAddress;
    qint64 machinePort;
    QTcpSocket *socket = nullptr;
};

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
    void establishConnection(const Connection&);
    void sendInitialInfo(const Connection&);

    Ui::MainWindow *ui = nullptr;
    QTcpServer     *server = nullptr;
    QUdpSocket     *broadcaster = nullptr;
    QMap<QString, Connection> connections;
};
#endif // MAINWINDOW_H
