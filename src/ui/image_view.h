#pragma once

#include <QQuickItem>
#include <QString>
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

    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void fitToView();
    Q_INVOKABLE void actualSize();

signals:
    void sourceChanged();
    void statusChanged();
    void zoomChanged();

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
