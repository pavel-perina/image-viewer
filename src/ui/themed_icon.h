#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QQuickPaintedItem>
#include <QSize>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QSvgRenderer;

namespace iv {

// The SVG of an icon with its placeholder colours replaced, as assets/icons/README.md describes
// them: white becomes ink and pure green (rgb(0,255,0)) accent, in fill, stroke and stop-color.
// Any other colour stays as drawn. Only the colour is used, not its alpha.
QByteArray recolorIcon(QByteArray svg, const QColor& ink, const QColor& accent);

// The icon qrc:/icons/<name>.svg in these colours, at this size in pixels. Null if there is no
// such icon.
QImage renderIcon(const QString& name, QSize size, const QColor& ink, const QColor& accent);

// One icon from assets/icons, in the theme's colours, as a QML item. It draws in a square, as
// large as fits, centred.
class ThemedIcon : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QColor ink READ ink WRITE setInk NOTIFY inkChanged)
    Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY accentChanged)
    Q_PROPERTY(bool valid READ isValid NOTIFY validChanged)

public:
    explicit ThemedIcon(QQuickItem* parent = nullptr);
    ~ThemedIcon() override;

    QString name() const { return m_name; }
    void setName(const QString& name);
    QColor ink() const { return m_ink; }
    void setInk(const QColor& ink);
    QColor accent() const { return m_accent; }
    void setAccent(const QColor& accent);
    // Whether the icon was found and read.
    bool isValid() const;

    void paint(QPainter* painter) override;

signals:
    void nameChanged();
    void inkChanged();
    void accentChanged();
    void validChanged();

private:
    void reload();

    QString m_name;
    QColor m_ink = Qt::white;
    QColor m_accent = QColor(0, 255, 0);
    std::unique_ptr<QSvgRenderer> m_renderer;
};

}  // namespace iv
