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
    // Which view the window is showing. The bar owns the choice because the tabs that make it live
    // here; what each one puts on screen is the window's business, not the bar's.
    enum class Tab { DeviceList, Settings };

    explicit TitleBar(QWidget *parent = nullptr);

    void setTitle(const QString &title);

    // Switches tab from outside - the tray menu's entries, which name the same two views. Emits
    // tabSelected() just as a click on the tab does.
    void setCurrentTab(Tab tab);

signals:
    // Emitted on every click on a tab, including a click on the one already current - the window
    // switching to the view it is already on costs nothing and is simpler than filtering here.
    void tabSelected(Tab tab);

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    QLabel      *iconLbl = nullptr;
    QLabel      *titleLbl = nullptr;
    QToolButton *deviceListTab = nullptr;
    QToolButton *settingsTab = nullptr;
    QToolButton *minimiseBtn = nullptr;
    QToolButton *maximiseBtn = nullptr;
    QToolButton *closeBtn = nullptr;
};

#endif // TITLEBAR_H
