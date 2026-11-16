#include "ui/color_choice_model.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

#include "core/color_choice.h"
#include "core/palette.h"

namespace iv {

struct ColorChoice::Impl {
    Rgba8 rgb{255, 255, 255, 255};
    Oklch lch = toOklch({255, 255, 255, 255});

    // From R, G and B, keeping the hue while there is no chroma.
    void fromRgb(Rgba8 colour) {
        rgb = colour;
        rgb.a = 255;
        const Oklch next = toOklch(rgb);
        lch = {next.L, next.C, next.C > 0 ? next.H : lch.H};
    }

    void fromLch() { rgb = fromOklch(lch.L, lch.C, lch.H); }
};

ColorChoice::ColorChoice(QObject* parent) : QObject(parent), m_impl(std::make_unique<Impl>()) {}

ColorChoice::~ColorChoice() = default;

QColor ColorChoice::colour() const { return {m_impl->rgb.r, m_impl->rgb.g, m_impl->rgb.b}; }

void ColorChoice::setColour(const QColor& colour) {
    const QColor rgb = colour.toRgb();
    const Rgba8 next{static_cast<std::uint8_t>(rgb.red()), static_cast<std::uint8_t>(rgb.green()), static_cast<std::uint8_t>(rgb.blue()), 255};
    if (next != m_impl->rgb) {
        m_impl->fromRgb(next);
        emit changed();
    }
}

QString ColorChoice::hex() const { return colour().name(QColor::HexRgb); }

void ColorChoice::setHex(const QString& hex) {
    static const QRegularExpression pattern(QStringLiteral("^#?([0-9a-fA-F]{6})$"));
    const auto match = pattern.match(hex.trimmed());
    if (match.hasMatch()) {
        setColour(QColor(QLatin1Char('#') + match.captured(1)));
    }
}

int ColorChoice::red() const { return m_impl->rgb.r; }
int ColorChoice::green() const { return m_impl->rgb.g; }
int ColorChoice::blue() const { return m_impl->rgb.b; }

namespace {

std::uint8_t channelValue(int value) { return static_cast<std::uint8_t>(std::clamp(value, 0, 255)); }

}  // namespace

void ColorChoice::setRed(int value) {
    Rgba8 next = m_impl->rgb;
    next.r = channelValue(value);
    if (next != m_impl->rgb) {
        m_impl->fromRgb(next);
        emit changed();
    }
}

void ColorChoice::setGreen(int value) {
    Rgba8 next = m_impl->rgb;
    next.g = channelValue(value);
    if (next != m_impl->rgb) {
        m_impl->fromRgb(next);
        emit changed();
    }
}

void ColorChoice::setBlue(int value) {
    Rgba8 next = m_impl->rgb;
    next.b = channelValue(value);
    if (next != m_impl->rgb) {
        m_impl->fromRgb(next);
        emit changed();
    }
}

double ColorChoice::lightness() const { return m_impl->lch.L; }
double ColorChoice::chroma() const { return m_impl->lch.C; }
double ColorChoice::hue() const { return m_impl->lch.H; }
double ColorChoice::maxChroma() const { return kMaxChooserChroma; }
bool ColorChoice::outside() const { return !showsInSrgb(m_impl->lch.L, m_impl->lch.C, m_impl->lch.H); }

void ColorChoice::setLightness(double value) {
    value = std::clamp(value, 0.0, 1.0);
    if (value != m_impl->lch.L) {
        m_impl->lch.L = value;
        m_impl->fromLch();
        emit changed();
    }
}

void ColorChoice::setChroma(double value) {
    value = std::clamp(value, 0.0, kMaxChooserChroma);
    if (value != m_impl->lch.C) {
        m_impl->lch.C = value;
        m_impl->fromLch();
        emit changed();
    }
}

void ColorChoice::setHue(double value) {
    value = std::fmod(std::fmod(value, 360.0) + 360.0, 360.0);
    if (value != m_impl->lch.H) {
        m_impl->lch.H = value;
        m_impl->fromLch();
        emit changed();
    }
}

QVariantList ColorChoice::ramps() const {
    QVariantList result;
    for (const ColorChannel channel : {ColorChannel::Red, ColorChannel::Green, ColorChannel::Blue, ColorChannel::Lightness,
                                       ColorChannel::Chroma, ColorChannel::Hue}) {
        QVariantList stops;
        for (const RampStop& stop : channelRamp(channel, m_impl->rgb, m_impl->lch, kRampStops)) {
            stops.append(QVariantMap{{QStringLiteral("colour"), QColor(stop.colour.r, stop.colour.g, stop.colour.b)},
                                     {QStringLiteral("outside"), stop.outside}});
        }
        result.append(QVariant(stops));
    }
    return result;
}

}  // namespace iv
