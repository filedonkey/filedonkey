#ifndef WINDOWSHADOW_H
#define WINDOWSHADOW_H

#include <QColor>
#include <QGraphicsDropShadowEffect>
#include <QWidget>

// The strip of window kept empty around the visible frame for the shadow to fall on. Everything
// inside it - title bar, content, whatever closes the window off at the bottom - is inset by this
// much, and the window is grown to match so the visible part stays the size it was meant to be.
#define SHADOW_MARGIN   18
#define SHADOW_BLUR     28
#define SHADOW_OFFSET_Y 6

// The shadow under a frameless window, as a widget to be dropped into it.
//
// It cannot go on the window itself: a graphics effect on a top-level window does not render. So it
// goes on a child instead - a plain widget the size and shape of the visible frame, stacked behind
// everything and painted the same colour as the corners it sits under. Its own body is covered by
// whatever the window puts on top of it; all that is ever seen of it is the blur it throws into the
// margin outside.
//
// Header-only and shared, the way ElidedLabel and revertButton() are. It was the main window's
// alone until the manual connect dialog stopped wearing the system's frame and had to draw its own,
// and the two are meant to be the same window furniture seen twice - a blur that differed between
// them would read as two applications.
//
// The caller still owns everything this cannot know: the frameless hint, the translucent background
// a rounded corner needs, the SHADOW_MARGIN of contents margin that leaves room for the blur, and
// keeping the returned widget at contentsRect() as the window is sized. What is here is only the
// part that is the same wherever it is used.
inline QWidget *windowShadow(QWidget *window)
{
    QWidget *layer = new QWidget(window);
    layer->setObjectName("windowShadow");
    layer->setAttribute(Qt::WA_StyledBackground, true);
    layer->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect(layer);
    shadow->setBlurRadius(SHADOW_BLUR);
    shadow->setOffset(0, SHADOW_OFFSET_Y);
    shadow->setColor(QColor(0, 0, 0, 160));
    layer->setGraphicsEffect(shadow);

    // Whatever the window has built by now is older than this widget and would otherwise sit under
    // it. Nothing may be drawn on top of the window's own contents.
    layer->lower();

    return layer;
}

#endif // WINDOWSHADOW_H
