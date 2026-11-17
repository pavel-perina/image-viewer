#include "ui/display_output.h"

#include <algorithm>

#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QtGui/qtguiglobal.h>
#include "ui/preferences.h"
#if QT_CONFIG(opengl)
#include <QOpenGLContext>
#endif

namespace iv {

int requestedOutputBits(QByteArrayView value) {
    return value.trimmed() == "10" ? 10 : 8;
}

int outputBits(const QSurfaceFormat& format) {
    const int bits = std::min({format.redBufferSize(), format.greenBufferSize(), format.blueBufferSize()});
    return bits > 0 ? bits : 8;
}

QString graphicsApiName(QSGRendererInterface::GraphicsApi api) {
    switch (api) {
    case QSGRendererInterface::Software: return QStringLiteral("software");
    case QSGRendererInterface::OpenGL: return QStringLiteral("OpenGL");
    case QSGRendererInterface::Direct3D11: return QStringLiteral("Direct3D 11");
    case QSGRendererInterface::Direct3D12: return QStringLiteral("Direct3D 12");
    case QSGRendererInterface::Vulkan: return QStringLiteral("Vulkan");
    case QSGRendererInterface::Metal: return QStringLiteral("Metal");
    default: return QStringLiteral("other");
    }
}

void requestOutputBits() {
    const int requested = qEnvironmentVariableIsSet("IV_OUTPUT_BITS")
        ? requestedOutputBits(qgetenv("IV_OUTPUT_BITS")) : Preferences::savedOutputBits();
    if (requested != 10) {
        return;
    }
    // A backend named in QSG_RHI_BACKEND wins, as Qt would let a request from here win over it.
    // On anything but OpenGL the window then stays at 8 bits, which About shows.
    if (qEnvironmentVariableIsEmpty("QSG_RHI_BACKEND")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    }
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setRedBufferSize(10);
    format.setGreenBufferSize(10);
    format.setBlueBufferSize(10);
    format.setAlphaBufferSize(2);
    QSurfaceFormat::setDefaultFormat(format);
}

DisplayOutput::DisplayOutput(QObject* parent)
    : QObject(parent), m_dither(qEnvironmentVariableIntValue("IV_NO_DITHER") == 0),
      m_environmentOverride(qEnvironmentVariableIsSet("IV_OUTPUT_BITS")),
      m_requestedBits(m_environmentOverride ? requestedOutputBits(qgetenv("IV_OUTPUT_BITS")) : Preferences::savedOutputBits()) {}

void DisplayOutput::watch(QQuickWindow* window) {
    if (!window) {
        return;
    }
    // Called on the render thread or this one; the answer comes to this one. The context's format
    // is the pixel format the driver gave.
    const auto read = [this, window] {
        QSGRendererInterface* renderer = window->rendererInterface();
        const QSGRendererInterface::GraphicsApi api = renderer->graphicsApi();
        int bits = 8;
#if QT_CONFIG(opengl)
        if (api == QSGRendererInterface::OpenGL) {
            const auto* context = static_cast<const QOpenGLContext*>(
                renderer->getResource(window, QSGRendererInterface::OpenGLContextResource));
            if (context) {
                bits = outputBits(context->format());
            }
        }
#endif
        QMetaObject::invokeMethod(this, [this, bits, name = graphicsApiName(api)] { setOutput(bits, name); },
                                  Qt::QueuedConnection);
    };
    // Again when the scene graph starts anew, as on another screen.
    connect(window, &QQuickWindow::sceneGraphInitialized, this, read, Qt::DirectConnection);
    // Showing a window on Windows may draw it at once, before this is called.
    if (window->isSceneGraphInitialized()) {
        read();
    }
}

void DisplayOutput::setOutput(int bits, const QString& api) {
    if (bits != m_bits || api != m_api) {
        m_bits = bits;
        m_api = api;
        emit bitsChanged();
    }
}

}  // namespace iv
