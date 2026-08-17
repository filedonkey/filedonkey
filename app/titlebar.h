#ifndef TITLEBAR_H
#define TITLEBAR_H

#include <QWidget>

QT_BEGIN_NAMESPACE
class QHBoxLayout;
class QLabel;
class QToolButton;
QT_END_NAMESPACE

// A window's own title bar, for use once the native one has been turned off with
// Qt::FramelessWindowHint. Drives the window it is placed in - it needs no wiring from the outside
// beyond being put at the top of it.
//
// Two of them are built, and Kind below is the whole of the difference. Both carry the application
// icon, the window's name and a close button, and both are dressed by one set of stylesheet rules,
// so the two windows are recognisably the same application drawn twice rather than two things that
// happen to look alike.
class TitleBar : public QWidget
{
    Q_OBJECT

public:
    // Which view the window is showing. The bar owns the choice because the tabs that make it live
    // here; what each one puts on screen is the window's business, not the bar's.
    enum class Tab { DeviceList, Settings };

    // What the bar carries besides the icon, the title and the close button.
    //
    // Window is the main window's: the two view tabs, and the minimise and maximise buttons a
    // window is expected to have. The maximise is permanently disabled, and there is no
    // double-click to maximise - the window is a fixed size - but it is kept, so the caption reads
    // the way Windows draws a fixed-size window rather than looking like a button is missing.
    //
    // Dialog is a dialog's: none of that. A dialog has one view, so there is nothing to tab
    // between; it cannot be minimised on its own, because it is modal and its window is the only
    // thing the application will answer; and there is nothing to maximise. What is left is a close
    // button, which is the one piece of window furniture a dialog does want - it is Cancel by
    // another route.
    enum class Kind { Window, Dialog };

    explicit TitleBar(Kind kind = Kind::Window, QWidget *parent = nullptr);

    void setTitle(const QString &title);

    // Switches tab from outside - the tray menu's entries, which name the same two views. Emits
    // tabSelected() just as a click on the tab does. Does nothing on a Dialog bar, which has no
    // tabs to switch between.
    void setCurrentTab(Tab tab);

signals:
    // Emitted on every click on a tab, including a click on the one already current - the window
    // switching to the view it is already on costs nothing and is simpler than filtering here.
    void tabSelected(Tab tab);

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    // The Kind::Window half of the bar: the tabs, the rule after them, and the minimise and
    // maximise buttons. Adds them to the bar's own layout, between the stretch that follows the
    // title and the close button that ends it.
    void buildWindowControls(QHBoxLayout *layout);

    QLabel      *iconLbl = nullptr;
    QLabel      *titleLbl = nullptr;
    QToolButton *closeBtn = nullptr;

    // Null on a Dialog bar, which carries none of them. Nothing here is dereferenced without being
    // checked - see setCurrentTab().
    QToolButton *deviceListTab = nullptr;
    QToolButton *settingsTab = nullptr;
    QToolButton *minimiseBtn = nullptr;
    QToolButton *maximiseBtn = nullptr;
};

#endif // TITLEBAR_H
