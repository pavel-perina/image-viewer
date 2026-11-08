#pragma once

#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace iv {

// Whether a key is held down in a window, which a Shortcut cannot tell as it has no release.
// Watches the window's events without taking them, and lets go when the window loses focus.
class KeyHold : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(int key READ key WRITE setKey NOTIFY keyChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool held READ held NOTIFY heldChanged)

public:
    using QObject::QObject;

    QObject* window() const { return m_window; }
    void setWindow(QObject* window);
    int key() const { return m_key; }
    void setKey(int key);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    bool held() const { return m_held; }

signals:
    void windowChanged();
    void keyChanged();
    void enabledChanged();
    void heldChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setHeld(bool held);

    QPointer<QObject> m_window;
    int m_key = 0;
    bool m_enabled = true;
    bool m_held = false;
};

}  // namespace iv
