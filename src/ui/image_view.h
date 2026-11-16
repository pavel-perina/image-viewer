#pragma once

#include <QPointF>
#include <QQuickItem>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QUrl>
#include <QVector4D>
#include <QImage>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <memory>

#include "core/perspective.h"
#include "core/shapes.h"

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
    // Drawn from half floats, with more than 8 bits that the output should dither.
    Q_PROPERTY(bool highPrecision READ highPrecision NOTIFY pageChanged)
    Q_PROPERTY(bool autoContrast READ autoContrast WRITE setAutoContrast NOTIFY pageChanged)
    // Auto Contrast for every image that can have it, rather than by its kind; turning it on
    // turns it on for the image shown too.
    Q_PROPERTY(bool keepAutoContrast READ keepAutoContrast WRITE setKeepAutoContrast NOTIFY keepAutoContrastChanged)
    // Rows of {section, label, value} describing the file and the current page.
    Q_PROPERTY(QVariantList details READ details NOTIFY detailsChanged)
    // The same in a line: "400x300 0.12MP 24bit PNG 12kB 2021-12-24 12:34:56", and
    // "1/60s f/4 ISO-500 EV -0.33", which is empty for an image without these.
    Q_PROPERTY(QString summary READ summary NOTIFY detailsChanged)
    Q_PROPERTY(QString cameraSummary READ cameraSummary NOTIFY detailsChanged)
    // The wheel asks for another image with navigateRequested, and zooms only with Ctrl.
    Q_PROPERTY(bool wheelNavigates READ wheelNavigates WRITE setWheelNavigates NOTIFY wheelNavigatesChanged)
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
    // Editing the page shown: whether it can be, and the state of its edits.
    Q_PROPERTY(bool editable READ editable NOTIFY editChanged)
    Q_PROPERTY(QString notEditableReason READ notEditableReason NOTIFY editChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY editChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY editChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY editChanged)
    // Whether the page shown is gray, which only takes the curves of a colour edit.
    Q_PROPERTY(bool grayImage READ grayImage NOTIFY statusChanged)
    // A colour edit drawn over the picture by a shader while it is adjusted: the lookup image
    // from colorLutImage and how to read it.
    Q_PROPERTY(bool colorPreviewActive READ colorPreviewActive NOTIFY colorPreviewChanged)
    Q_PROPERTY(QImage colorPreviewLut READ colorPreviewLut NOTIFY colorPreviewChanged)
    Q_PROPERTY(int colorPreviewCubeSize READ colorPreviewCubeSize NOTIFY colorPreviewChanged)
    Q_PROPERTY(double colorPreviewChroma READ colorPreviewChroma NOTIFY colorPreviewChanged)
    Q_PROPERTY(bool colorPreviewTone READ colorPreviewTone NOTIFY colorPreviewChanged)
    Q_PROPERTY(bool colorPreviewLightness READ colorPreviewLightness NOTIFY colorPreviewChanged)
    Q_PROPERTY(bool colorPreviewGamutMap READ colorPreviewGamutMap NOTIFY colorPreviewChanged)
    Q_PROPERTY(QVector4D colorPreviewDomainMin READ colorPreviewDomainMin NOTIFY colorPreviewChanged)
    Q_PROPERTY(QVector4D colorPreviewDomainMax READ colorPreviewDomainMax NOTIFY colorPreviewChanged)
    // The name of the loaded .cube file, empty without one.
    Q_PROPERTY(QString colorCubeName READ colorCubeName NOTIFY colorPreviewChanged)
    // Another view that zooms and scrolls with this one while both show pictures of the same size,
    // for comparing them. Set it on both.
    Q_PROPERTY(iv::ImageView* linkedView READ linkedView WRITE setLinkedView NOTIFY linkedViewChanged)

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
    bool highPrecision() const;
    bool autoContrast() const;
    void setAutoContrast(bool enabled);
    bool keepAutoContrast() const;
    void setKeepAutoContrast(bool keep);
    QVariantList details() const;
    QString summary() const;
    QString cameraSummary() const;
    bool wheelNavigates() const;
    void setWheelNavigates(bool navigates);
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
    bool editable() const;
    QString notEditableReason() const;
    bool modified() const;
    bool canUndo() const;
    bool canRedo() const;
    bool grayImage() const;
    bool colorPreviewActive() const;
    QImage colorPreviewLut() const;
    int colorPreviewCubeSize() const;
    double colorPreviewChroma() const;
    bool colorPreviewTone() const;
    bool colorPreviewLightness() const;
    bool colorPreviewGamutMap() const;
    QVector4D colorPreviewDomainMin() const;
    QVector4D colorPreviewDomainMax() const;
    QString colorCubeName() const;
    ImageView* linkedView() const;
    void setLinkedView(ImageView* view);

    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void fitToView();
    Q_INVOKABLE void actualSize();
    // Loads the file again, after it changed on disk.
    Q_INVOKABLE void reload();
    Q_INVOKABLE void panBy(double dx, double dy);  // in item coordinates

    // Between pixel coordinates of the image as shown, upright and edited, and item coordinates.
    Q_INVOKABLE QPointF imageToItem(QPointF point) const;
    Q_INVOKABLE QPointF itemToImage(QPointF point) const;

    // Keeps a rectangle of the image as shown.
    Q_INVOKABLE void crop(const QRect& rect);
    // Scales the image as shown to exactly that size; with fill, a different ratio crops the
    // middle instead of stretching. resizeProblem says why a size cannot be used, or is empty.
    Q_INVOKABLE void resizeImage(int width, int height, bool fill);
    Q_INVOKABLE QString resizeProblem(int width, int height) const;
    // Turns the picture gray: 0 weighs R, G and B as JPEG does, 1 keeps Oklab lightness.
    Q_INVOKABLE void grayscale(int method);
    // Burns annotation shapes into the picture, as one edit.
    void draw(const std::vector<Shape>& shapes);
    // Before drawing: reads the picture at full size in the background and reports drawReady,
    // after which drawPreview has pixels to work on. endDraw lets go of them.
    Q_INVOKABLE void prepareDraw();
    Q_INVOKABLE void endDraw();
    // The part of the picture a shape changes, as it will look once applied and as it is
    // shown, with where it is in pixels of the image. For the shapes whose result depends on
    // what is under them. A large part comes smaller, to be drawn stretched. Null until
    // drawReady.
    QImage drawPreview(const Shape& shape, QRect* where) const;
    // Colour adjustment, from a map in which every key is optional. Levels: black and white, from
    // 0 to 1 of the samples as stored, gamma above 1 lightening their midtones, and outBlack and
    // outWhite, the range they map to. In Oklab, keeping colours: brightness added to lightness,
    // contrast scaling it about the middle, lightness a gamma for it, and saturation scaling
    // chroma, 1 leaving it; gamutMap, true unless given, fits colours beyond the range by
    // chroma instead of clipping them. A loaded cube comes last. previewColors shows them by the shader until
    // endColorPreview or another picture is shown; adjustColors applies them, and the preview
    // stays until the result replaces it.
    Q_INVOKABLE void previewColors(const QVariantMap& settings);
    Q_INVOKABLE void endColorPreview();
    Q_INVOKABLE void adjustColors(const QVariantMap& settings);
    Q_INVOKABLE QString loadColorCube(const QUrl& file);
    Q_INVOKABLE void clearColorCube();
    // Shows what another view shows, sharing its decoded file rather than reading it again: with
    // original, the file's page without edits; otherwise its picture as it is now, without a
    // colour preview. Nothing is shown if the other view has no picture.
    Q_INVOKABLE void showPictureOf(iv::ImageView* other, bool original);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // Back to the page as it is in the file.
    Q_INVOKABLE void discardEdits();
    // The edited page, to be saved; empty without edits. Saving it is reported with markSaved.
    Q_INVOKABLE QVariant editedImage() const;
    // Unless it has been edited further since the image was taken.
    Q_INVOKABLE void markSaved(const QVariant& image);

    // Reads the pixel at a point of the image as shown, at full size, and reports it with
    // probed: x and y as shown, hex, values and fractions as stored, depth, srgbHex and
    // srgbValues when the colour profile makes them differ, and color for a swatch.
    Q_INVOKABLE void probe(QPointF point);

    // Straightening the image as shown, as if the camera had been turned. prepareStraighten reads
    // a reduced copy for previews and finding lines, and reports straightenReady; until then
    // detectStraightLines and straightenPreview give nothing. Angles are in degrees, lines are
    // {x1, y1, x2, y2} in pixels of the image as shown, and a focal length of 0 assumes 28 mm.
    Q_INVOKABLE void prepareStraighten();
    // Long straight edges near vertical or horizontal, longest first.
    Q_INVOKABLE QVariantList detectStraightLines() const;
    // roll, pitch and yaw that make the lines vertical or horizontal, and used, which of them
    // were kept; robust leaves out the ones that disagree. mode: 0 rotation only, 1 with
    // verticals, 2 with horizontals too.
    Q_INVOKABLE QVariantMap fitStraightLines(const QVariantList& lines, int mode, double focal35mm, bool robust) const;
    // From the camera data, or 0 when unknown.
    Q_INVOKABLE double focalLength35mm() const;
    Q_INVOKABLE QSize straightenedSize(double roll, double pitch, double yaw, double focal35mm, bool keepRatio, bool crop = true) const;
    // Without crop the whole turned picture is kept, with transparent corners. Nothing happens
    // if the result would be too large to be saved.
    Q_INVOKABLE void straighten(double roll, double pitch, double yaw, double focal35mm, bool keepRatio, bool crop = true);
    // The reduced copy straightened; null until it is ready.
    QImage straightenPreview(const Tilt& tilt, double focal35mm, bool keepRatio, bool crop = true) const;
    // The corners of what straightening keeps, in pixels of the image as shown.
    std::array<QPointF, 4> straightenKept(const Tilt& tilt, double focal35mm, bool keepRatio, bool crop = true) const;

signals:
    void sourceChanged();
    void statusChanged();
    void zoomChanged();
    void pageChanged();
    void detailsChanged();
    void histogramChanged();
    void resampleChanged();
    void wheelNavigatesChanged();
    void keepAutoContrastChanged();
    void editChanged();
    void colorPreviewChanged();
    void linkedViewChanged();
    void probed(const QVariantMap& pixel);
    void straightenReady();
    void drawReady();
    void navigateRequested(int steps);  // positive towards the next image

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace iv
