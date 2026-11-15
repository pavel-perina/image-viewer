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
    // Tabular figures: every digit as wide, so that changing numbers keep still and columns line up.
    font.setFeature(QFont::Tag("tnum"), 1);
    if (pointSize) {
        font.setPointSizeF(*pointSize);
    }
    QGuiApplication::setFont(font);
}

}  // namespace iv
