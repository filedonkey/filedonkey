#ifndef ELIDEDLABEL_H
#define ELIDEDLABEL_H

#include <QLabel>
#include <QPainter>

// A label that shortens what it draws rather than asking the layout for room it cannot have. The
// window is a fixed size and nothing in it scrolls sideways, so a long machine name or a long path
// would otherwise be cut off mid-letter at the edge of its row.
//
// Elided at paint time and never through setText(): shortening the text would shrink the label's
// own size hint, the layout would hand it less room next time round, and it would elide further on
// every pass. text() stays the full string, so it is what each repaint - and the tooltip - starts
// from.
//
// Header-only, and shared by the device list and the settings page. It was the list's alone until
// the settings page grew a path to show, which wants exactly the same thing of it.
class ElidedLabel : public QLabel
{
public:
    using QLabel::QLabel;

    // What lets the layout shrink it at all. QLabel's own minimum for one unwrapped line is the
    // whole string, and that would push the row - and with it the window - wider.
    QSize minimumSizeHint() const override
    {
        return QSize(0, QLabel::minimumSizeHint().height());
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);

        // The stylesheet's `color:` reaches a label through its palette, and QPainter's default pen
        // does not read it - drawing without this would put every label back to black.
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(rect(), alignment(), fontMetrics().elidedText(text(), Qt::ElideRight, width()));
    }
};

#endif // ELIDEDLABEL_H
