#include "ui/program_font.h"

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQuickWindow>

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
    // Every text as the platform draws it, hinted and with ClearType on Windows, as text fields
    // and what QPainter draws already are. Qt Quick's own distance fields, its default, are
    // greyscale and soft at these sizes, and the window mixed the two.
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
}

}  // namespace iv
