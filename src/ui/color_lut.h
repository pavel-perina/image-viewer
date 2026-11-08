#pragma once

#include <QImage>
#include <QQuickItem>
#include <QSGTextureProvider>
#include <QtQml/qqmlregistration.h>

#include "core/color_edit.h"

namespace iv {

// The rows a colour edit's curves take below its cube in the preview's lookup image, and how many
// samples each has.
constexpr int kLutCurveSamples = 256;

// A colour edit as one image for the preview shader: the cube's slices side by side, blue picking
// the slice, then a row for the tone curve and one for the lightness curve. Half floats, so curves
// and cube keep more than 8 bits.
QImage colorLutImage(const ColorEdit& edit);

class ColorLutProvider;

// Hands an image to a ShaderEffect as a texture, exactly as it is: no filtering, no mipmaps.
class ColorLutTexture : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QImage image READ image WRITE setImage NOTIFY imageChanged)

public:
    explicit ColorLutTexture(QQuickItem* parent = nullptr);
    ~ColorLutTexture() override;

    QImage image() const { return m_image; }
    void setImage(const QImage& image);

    bool isTextureProvider() const override { return true; }
    QSGTextureProvider* textureProvider() const override;

signals:
    void imageChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
    void releaseResources() override;

private:
    void releaseProvider();

    QImage m_image;
    bool m_dirty = true;
    mutable ColorLutProvider* m_provider = nullptr;
};

}  // namespace iv
