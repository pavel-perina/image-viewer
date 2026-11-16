#pragma once

#include <QByteArrayView>
#include <QObject>
#include <QSGRendererInterface>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QQuickWindow;
class QSurfaceFormat;

namespace iv {

// How many bits per channel the window shows, and whether the last pass dithers.
//
// Qt Quick makes 8-bit windows unless asked. IV_OUTPUT_BITS=10 asks for 10 bits, and for OpenGL,
// as through Direct3D, Vulkan and Metal Qt Quick offers more than 8 bits only for HDR. Whether
// the driver gives them is learnt from the window once it draws. IV_NO_DITHER=1 turns off the
// dithering of the last pass, which on an 8-bit window would hide that 10 bits are missing.

// The bits asked for by a value of IV_OUTPUT_BITS: 10, or 8 for anything else.
int requestedOutputBits(QByteArrayView value);

// The bits per channel of a surface, the least of red, green and blue; 8 when it does not say.
int outputBits(const QSurfaceFormat& format);

// The name of a graphics API, as About shows it.
QString graphicsApiName(QSGRendererInterface::GraphicsApi api);

// Asks for 10 bits per channel, and OpenGL unless QSG_RHI_BACKEND names another, when
// IV_OUTPUT_BITS=10. Call before the application exists, as both must be set before any window is.
void requestOutputBits();

class DisplayOutput : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int bits READ bits NOTIFY bitsChanged)
    Q_PROPERTY(bool dither READ dither CONSTANT)
    Q_PROPERTY(QString api READ api NOTIFY bitsChanged)  // empty until the window draws

public:
    explicit DisplayOutput(QObject* parent = nullptr);

    int bits() const { return m_bits; }
    QString api() const { return m_api; }
    bool dither() const { return m_dither; }

    // Learns the bits of the window's buffer, now if it has drawn and whenever its scene graph
    // starts. Stays at 8 for anything but OpenGL.
    Q_INVOKABLE void watch(QQuickWindow* window);

signals:
    void bitsChanged();

private:
    void setOutput(int bits, const QString& api);

    int m_bits = 8;
    QString m_api;
    bool m_dither = true;
};

}  // namespace iv
