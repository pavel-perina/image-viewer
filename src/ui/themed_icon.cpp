#include "themed_icon.h"

#include <QFile>
#include <QHash>
#include <QPainter>
#include <QRegularExpression>
#include <QSvgRenderer>

#include <algorithm>

namespace iv {

namespace {

// The SVG text of each icon, read once, as several buttons show the same one.
QByteArray iconSource(const QString& name) {
    static QHash<QString, QByteArray> sources;
    if (const auto found = sources.constFind(name); found != sources.constEnd()) {
        return found.value();
    }
    QFile file(QStringLiteral(":/icons/") + name + QStringLiteral(".svg"));
    const QByteArray source = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    sources.insert(name, source);
    return source;
}

}  // namespace

QByteArray recolorIcon(QByteArray svg, const QColor& ink, const QColor& accent) {
    // A colour property, in a style ("fill:white") or as an attribute (fill="white"), then the
    // placeholder in any of the ways an editor writes it.
    static const QRegularExpression placeholder(
        QStringLiteral(R"(((?:fill|stroke|stop-color)\s*(?::|=\s*["'])\s*))"
                       R"((white|#fff(?:fff)?|rgb\(\s*255\s*,\s*255\s*,\s*255\s*\)|)"
                       R"(lime|#0f0|#00ff00|rgb\(\s*0\s*,\s*255\s*,\s*0\s*\))(?![0-9a-z]))"),
        QRegularExpression::CaseInsensitiveOption);
    const QString text = QString::fromUtf8(svg);
    QString result;
    result.reserve(text.size());
    qsizetype done = 0;
    for (auto it = placeholder.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        const QString value = match.captured(2).toLower().remove(u' ');
        const bool white = value == u"white" || value == u"#fff" || value == u"#ffffff" || value == u"rgb(255,255,255)";
        result += QStringView(text).mid(done, match.capturedStart(2) - done);
        result += (white ? ink : accent).name(QColor::HexRgb);
        done = match.capturedEnd(2);
    }
    result += QStringView(text).mid(done);
    return result.toUtf8();
}

QImage renderIcon(const QString& name, QSize size, const QColor& ink, const QColor& accent) {
    QSvgRenderer renderer(recolorIcon(iconSource(name), ink, accent));
    if (!renderer.isValid() || size.isEmpty()) {
        return {};
    }
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.render(&painter);
    return image;
}

ThemedIcon::ThemedIcon(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setImplicitSize(24, 24);
    setAntialiasing(true);
}

ThemedIcon::~ThemedIcon() = default;

void ThemedIcon::setName(const QString& name) {
    if (name != m_name) {
        m_name = name;
        reload();
        emit nameChanged();
    }
}

void ThemedIcon::setInk(const QColor& ink) {
    if (ink != m_ink) {
        m_ink = ink;
        reload();
        emit inkChanged();
    }
}

void ThemedIcon::setAccent(const QColor& accent) {
    if (accent != m_accent) {
        m_accent = accent;
        reload();
        emit accentChanged();
    }
}

bool ThemedIcon::isValid() const { return m_renderer && m_renderer->isValid(); }

void ThemedIcon::reload() {
    const bool wasValid = isValid();
    m_renderer.reset();
    if (!m_name.isEmpty()) {
        const QByteArray source = iconSource(m_name);
        if (!source.isEmpty()) {
            m_renderer = std::make_unique<QSvgRenderer>(recolorIcon(source, m_ink, m_accent));
        }
    }
    if (isValid() != wasValid) {
        emit validChanged();
    }
    update();
}

void ThemedIcon::paint(QPainter* painter) {
    if (!isValid()) {
        return;
    }
    const qreal side = std::min(width(), height());
    m_renderer->render(painter, QRectF((width() - side) / 2, (height() - side) / 2, side, side));
}

}  // namespace iv
