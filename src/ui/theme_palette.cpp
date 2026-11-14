#include "ui/theme_palette.h"

#include <QColor>

namespace iv {

ThemePalette::ThemePalette(QObject* parent) : QObject(parent) {
    update();
}

void ThemePalette::setHue(double hue) {
    if (hue != m_choices.hue) {
        m_choices.hue = hue;
        update();
    }
}

void ThemePalette::setAccentChroma(double chroma) {
    if (chroma != m_choices.accentChroma) {
        m_choices.accentChroma = chroma;
        update();
    }
}

void ThemePalette::setSurfaceChroma(double chroma) {
    if (chroma != m_choices.surfaceChroma) {
        m_choices.surfaceChroma = chroma;
        update();
    }
}

void ThemePalette::update() {
    m_colours.clear();
    for (const PaletteRole& role : derivePalette(m_choices)) {
        m_colours.insert(QString::fromUtf8(role.name.data(), static_cast<qsizetype>(role.name.size())),
                         QColor(role.colour.r, role.colour.g, role.colour.b, role.colour.a));
    }
    emit changed();
}

}  // namespace iv
