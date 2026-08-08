#ifndef TITLEBAR_H
#define TITLEBAR_H

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QToolButton;
QT_END_NAMESPACE

// The window's own title bar, for use once the native one has been turned off with
// Qt::FramelessWindowHint. Drives the window it is placed in - it needs no wiring from the
// outside beyond being handed to QMainWindow::setMenuWidget().
//
// The maximise button is here but permanently disabled, and there is no double-click to maximise:
// the window is a fixed size. It is kept so the caption reads the way Windows draws a fixed-size
// dialog - three buttons with the middle one greyed - rather than looking like a button is missing.
class TitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);

    void setTitle(const QString &title);

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    QLabel      *iconLbl = nullptr;
    QLabel      *titleLbl = nullptr;
    QToolButton *minimiseBtn = nullptr;
    QToolButton *maximiseBtn = nullptr;
    QToolButton *closeBtn = nullptr;
};

#endif // TITLEBAR_H
