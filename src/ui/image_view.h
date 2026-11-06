#pragma once

#include <QPointF>
#include <QQuickItem>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace iv {

// Shows one image, drawing only the visible tiles of the suitable pyramid level. Decoding
// runs in the background and the previous image stays up until the new one has a preview.
class ImageView : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged)
    Q_PROPERTY(int imageWidth READ imageWidth NOTIFY statusChanged)
    Q_PROPERTY(int imageHeight READ imageHeight NOTIFY statusChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY zoomChanged)
    // The part of the image in view, as fractions of it. Changes with panning too.
    Q_PROPERTY(QRectF visibleArea READ visibleArea NOTIFY zoomChanged)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY pageChanged)
    Q_PROPERTY(bool contrastAdjustable READ contrastAdjustable NOTIFY pageChanged)
    Q_PROPERTY(bool autoContrast READ autoContrast WRITE setAutoContrast NOTIFY pageChanged)
    // Rows of {section, label, value} describing the file and the current page.
    Q_PROPERTY(QVariantList details READ details NOTIFY detailsChanged)
    // Computing it costs a read of the image, so it only happens while this is enabled.
    Q_PROPERTY(bool histogramEnabled READ histogramEnabled WRITE setHistogramEnabled NOTIFY histogramChanged)
    // Opaque; meant for HistogramView.histogram. Empty until computed.
    Q_PROPERTY(QVariant histogram READ histogram NOTIFY histogramChanged)
    // Whether to draw through ResampledView where that helps. The rest describes how, for that
    // item, and resampleActive says whether it applies now: it needs the GPU, and some zooms
    // show pixels as they are.
    Q_PROPERTY(bool resampling READ resampling WRITE setResampling NOTIFY resampleChanged)
    Q_PROPERTY(bool resampleActive READ resampleActive NOTIFY resampleChanged)
    Q_PROPERTY(QSize resampleTextureSize READ resampleTextureSize NOTIFY resampleChanged)
    Q_PROPERTY(QPointF resamplePhase READ resamplePhase NOTIFY resampleChanged)
    Q_PROPERTY(QRectF resampleBounds READ resampleBounds NOTIFY resampleChanged)
    Q_PROPERTY(QRectF resampleImageRect READ resampleImageRect NOTIFY resampleChanged)
    Q_PROPERTY(double resampleRatio READ resampleRatio NOTIFY resampleChanged)
    Q_PROPERTY(double resampleSpacing READ resampleSpacing NOTIFY resampleChanged)
    Q_PROPERTY(double resampleDevicePixelRatio READ resampleDevicePixelRatio NOTIFY resampleChanged)

public:
    enum class Status { Null, Loading, Ready, Error };
    Q_ENUM(Status)

    explicit ImageView(QQuickItem* parent = nullptr);
    ~ImageView() override;

    QString source() const;
    void setSource(const QString& path);
    Status status() const;
    QString errorString() const;
    int imageWidth() const;
    int imageHeight() const;
    double zoom() const;  // device pixels per image pixel
    QRectF visibleArea() const;
    int page() const;
    void setPage(int page);
    int pageCount() const;
    bool contrastAdjustable() const;  // whether the image has more than 8 bits to map
    bool autoContrast() const;
    void setAutoContrast(bool enabled);
    QVariantList details() const;
    bool histogramEnabled() const;
    void setHistogramEnabled(bool enabled);
    QVariant histogram() const;
    bool resampling() const;
    void setResampling(bool enabled);
    bool resampleActive() const;
    QSize resampleTextureSize() const;
    QPointF resamplePhase() const;
    QRectF resampleBounds() const;
    QRectF resampleImageRect() const;  // the image in the view, from 0 to 1
    double resampleRatio() const;
    double resampleSpacing() const;
    double resampleDevicePixelRatio() const;

    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void fitToView();
    Q_INVOKABLE void actualSize();

signals:
    void sourceChanged();
    void statusChanged();
    void zoomChanged();
    void pageChanged();
    void detailsChanged();
    void histogramChanged();
    void resampleChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace iv
