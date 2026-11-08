#include "ui/key_hold.h"

#include <QEvent>
#include <QKeyEvent>

namespace iv {

void KeyHold::setWindow(QObject* window) {
    if (window == m_window) {
        return;
    }
    if (m_window) {
        m_window->removeEventFilter(this);
    }
    m_window = window;
    if (m_window) {
        m_window->installEventFilter(this);
    }
    setHeld(false);
    emit windowChanged();
}

void KeyHold::setKey(int key) {
    if (key != m_key) {
        m_key = key;
        setHeld(false);
        emit keyChanged();
    }
}

void KeyHold::setEnabled(bool enabled) {
    if (enabled != m_enabled) {
        m_enabled = enabled;
        if (!enabled) {
            setHeld(false);
        }
        emit enabledChanged();
    }
}

bool KeyHold::eventFilter(QObject* watched, QEvent* event) {
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == m_key && !key->isAutoRepeat()) {
            const bool press = event->type() == QEvent::KeyPress;
            setHeld(press && m_enabled && key->modifiers() == Qt::NoModifier);
        }
        break;
    }
    case QEvent::FocusOut:
    case QEvent::WindowDeactivate:
        setHeld(false);
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void KeyHold::setHeld(bool held) {
    if (held != m_held) {
        m_held = held;
        emit heldChanged();
    }
}

}  // namespace iv
