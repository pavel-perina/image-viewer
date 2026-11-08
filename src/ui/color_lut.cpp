#include "ui/color_lut.h"

#include <QQuickWindow>
#include <QRunnable>
#include <QSGTexture>
#include <QtGui/qrgbafloat.h>

#include <algorithm>

namespace iv {

class ColorLutProvider : public QSGTextureProvider {
public:
    QSGTexture* texture() const override { return m_texture.get(); }
    void setTexture(QSGTexture* texture) {
        m_texture.reset(texture);
        if (m_texture) {
            m_texture->setFiltering(QSGTexture::Nearest);
            m_texture->setMipmapFiltering(QSGTexture::None);
            m_texture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
            m_texture->setVerticalWrapMode(QSGTexture::ClampToEdge);
        }
        emit textureChanged();
    }

private:
    std::unique_ptr<QSGTexture> m_texture;
};

namespace {

QRgbaFloat16 half(float r, float g, float b) {
    return QRgbaFloat16{qfloat16(r), qfloat16(g), qfloat16(b), qfloat16(1.0F)};
}

void writeCurve(QImage& image, int row, const Curve& curve) {
    auto* line = reinterpret_cast<QRgbaFloat16*>(image.scanLine(row));
    for (int i = 0; i < kLutCurveSamples; ++i) {
        const float v = curve(static_cast<float>(i) / (kLutCurveSamples - 1));
        line[i] = half(v, v, v);
    }
}

}  // namespace

QImage colorLutImage(const ColorEdit& edit) {
    const int n = edit.cube ? edit.cube->size : 0;
    const int width = std::max(n * n, kLutCurveSamples);
    QImage image(width, n + 2, QImage::Format_RGBA16FPx4);
    image.fill(Qt::black);
    for (int g = 0; g < n; ++g) {
        auto* line = reinterpret_cast<QRgbaFloat16*>(image.scanLine(g));
        for (int b = 0; b < n; ++b) {
            for (int r = 0; r < n; ++r) {
                const float* entry = &edit.cube->rgb[(static_cast<std::size_t>(b * n + g) * static_cast<std::size_t>(n) +
                                                      static_cast<std::size_t>(r)) *
                                                     3];
                line[b * n + r] = half(entry[0], entry[1], entry[2]);
            }
        }
    }
    writeCurve(image, n, edit.tone);
    writeCurve(image, n + 1, edit.lightness);
    return image;
}

ColorLutTexture::ColorLutTexture(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents); }

ColorLutTexture::~ColorLutTexture() { releaseProvider(); }

void ColorLutTexture::releaseProvider() {
    if (!m_provider) {
        return;
    }
    // The texture belongs to the render thread, and goes with the provider there.
    class Cleanup : public QRunnable {
    public:
        explicit Cleanup(ColorLutProvider* provider) : m_provider(provider) {}
        void run() override { delete m_provider; }

    private:
        ColorLutProvider* m_provider;
    };
    if (window()) {
        window()->scheduleRenderJob(new Cleanup(m_provider), QQuickWindow::BeforeSynchronizingStage);
    } else {
        delete m_provider;
    }
    m_provider = nullptr;
}

void ColorLutTexture::setImage(const QImage& image) {
    m_image = image;
    m_dirty = true;
    update();
    emit imageChanged();
}

QSGTextureProvider* ColorLutTexture::textureProvider() const {
    if (!m_provider) {
        m_provider = new ColorLutProvider;
    }
    return m_provider;
}

QSGNode* ColorLutTexture::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    delete oldNode;
    if (m_dirty && window()) {
        m_dirty = false;
        auto* provider = static_cast<ColorLutProvider*>(textureProvider());
        provider->setTexture(m_image.isNull() ? nullptr : window()->createTextureFromImage(m_image));
    }
    return nullptr;
}

void ColorLutTexture::releaseResources() {
    releaseProvider();
    m_dirty = true;
}

}  // namespace iv
