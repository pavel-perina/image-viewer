#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace iv {

// Shortcuts as the system writes them, for menus to show beside their items.
class KeyNames : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    // Takes what an Action's shortcut holds: text such as "Ctrl+L", or a StandardKey, of which
    // the first binding is shown. Empty for anything else.
    Q_INVOKABLE static QString text(const QVariant& shortcut);
};

}  // namespace iv
