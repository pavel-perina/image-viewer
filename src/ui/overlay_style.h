#pragma once

#include <QColor>
#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace iv {

// The colours the tools draw on the image with. QML makes one and fills it from Theme, so
// that Theme stays the one place for colours; the values here are only for a tool without one.
class OverlayStyle : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // Lines, and the grip chosen.
    Q_PROPERTY(QColor line MEMBER m_line NOTIFY changed)
    // Under and around them, so that they show on any picture.
    Q_PROPERTY(QColor outline MEMBER m_outline NOTIFY changed)
    // Over what a tool leaves out.
    Q_PROPERTY(QColor dim MEMBER m_dim NOTIFY changed)
    // A grid over a picture, to judge it by without hiding it.
    Q_PROPERTY(QColor faint MEMBER m_faint NOTIFY changed)
    // Around a picture a tool shows in place of the view's.
    Q_PROPERTY(QColor surround MEMBER m_surround NOTIFY changed)

public:
    using QObject::QObject;

    QColor line() const { return m_line; }
    QColor outline() const { return m_outline; }
    QColor dim() const { return m_dim; }
    QColor faint() const { return m_faint; }
    QColor surround() const { return m_surround; }

signals:
    void changed();

private:
    QColor m_line{Qt::white};
    QColor m_outline{0, 0, 0, 200};
    QColor m_dim{0, 0, 0, 150};
    QColor m_faint{255, 255, 255, 70};
    QColor m_surround{32, 32, 32};
};

}  // namespace iv
