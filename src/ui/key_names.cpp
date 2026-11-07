#include "ui/key_names.h"

#include <QKeySequence>

namespace iv {

QString KeyNames::text(const QVariant& shortcut) {
    QKeySequence sequence;
    if (shortcut.typeId() == QMetaType::QString) {
        sequence = QKeySequence(shortcut.toString(), QKeySequence::PortableText);
    } else if (shortcut.typeId() == QMetaType::QKeySequence) {
        sequence = shortcut.value<QKeySequence>();
    } else if (bool isNumber = false; shortcut.isValid() && (shortcut.toInt(&isNumber), isNumber)) {
        sequence = QKeySequence::keyBindings(static_cast<QKeySequence::StandardKey>(shortcut.toInt())).value(0);
    }
    return sequence.toString(QKeySequence::NativeText);
}

}  // namespace iv
