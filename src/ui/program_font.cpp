#include "ui/program_font.h"

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>

namespace iv {

void useProgramFont(std::optional<double> pointSize) {
    QFont font = QGuiApplication::font();
    if (const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/InterVariable.ttf")); id >= 0) {
        font.setFamilies(QFontDatabase::applicationFontFamilies(id));
    }
    if (pointSize) {
        font.setPointSizeF(*pointSize);
    }
    QGuiApplication::setFont(font);
}

}  // namespace iv
