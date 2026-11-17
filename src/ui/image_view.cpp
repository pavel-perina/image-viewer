#include "ui/image_view.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLocale>
#include <QColor>
#include <QCursor>
#include <QMatrix4x4>
#include <QtMath>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPixmap>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QSGTransformNode>
#include <QPointingDevice>
#include <QWheelEvent>
#include <QtDebug>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <memory>
#include <new>
#include <numbers>
#include <set>
#include <span>
#include <utility>
#include <vector>

#include "core/color.h"
#include "core/color_edit.h"
#include "core/document.h"
#include "core/histogram.h"
#include "core/image_details.h"
#include "core/image_ops.h"
#include "core/image_source.h"
#include "core/perspective.h"
#include "ui/color_lut.h"
#include "core/pixel_probe.h"
#include "core/viewport.h"
#include "core/work_queue.h"
#include "ui/color_settings.h"
#include "ui/edited_image.h"
#include "ui/histogram_handle.h"
#include "ui/qimage_bridge.h"
#include "ui/shape_painter.h"

namespace iv {

namespace {

// Whole steps from a wheel's turning, adding up the small steps a touchpad sends; turning back
// starts over, so that a little jitter the other way does not count.
int wholeSteps(int& turned, int delta, int perStep) {
    turned = (turned < 0) == (delta < 0) ? turned + delta : delta;
    const int steps = turned / perStep;
    turned -= steps * perStep;
    return steps;
}

// A touchpad, told apart from a mouse wheel: by the device where the system says, else by the
// phases of a scroll and the distance in pixels, which wheels do not send.
bool fromTouchpad(const QWheelEvent& event) {
    const QPointingDevice* device = event.pointingDevice();
    return (device && device->type() == QInputDevice::DeviceType::TouchPad) || event.phase() != Qt::NoScrollPhase
           || !event.pixelDelta().isNull();
}

// The pixels of a tile's texture. Half floats where the image has more than 8 bits, so that the
// colour preview and sharp resampling work on all of them; 8 bits for the software renderer,
// which shows no more, and for 8-bit images.
QImage tileImage(const ImageBuffer& tile, DisplayRange range, const ColorTransform* color, bool wide, const Backdrop& backdrop) {
    if (!wide) {
        return toQImage(toDisplay(tile, range, color, backdrop));
    }
    const ImageBuffer shown = toDisplayWide(tile, range, color, backdrop);
    if (shown.format() != PixelFormat::Rgba16) {
        return toQImage(shown);
    }
    const QImage pixels(reinterpret_cast<const uchar*>(shown.data()), shown.width(), shown.height(),
                        static_cast<qsizetype>(shown.stride()), QImage::Format_RGBA64);
    return pixels.convertToFormat(QImage::Format_RGBA16FPx4);  // a copy, so shown may go
}

constexpr int kOpenPriority = 1000;
constexpr int kTilePriority = 500;
constexpr int kHistogramPriority = 100;
constexpr int kProbePriority = 900;
constexpr int kStraightenPriority = 800;
constexpr int kStraightenBaseEdge = 1200;  // the long edge of the copy straightening works on
constexpr double kDefaultFocal35mm = 28;
// Degrees from vertical or horizontal for a line found. Pictures needing more are rare, and
// diagonal edges, such as traces on a board, would otherwise come in.
constexpr double kMaxLineLean = 15;
// A notch of a mouse wheel, in eighths of a degree: one zoom step, three to a doubling.
constexpr int kWheelNotch = 120;
// A touchpad zooms smoothly instead, a doubling for a swipe of three notches' worth, and holds
// at 25, 50, 100, 200 and 400% for a tenth of a doubling on each side, to be landed on.
constexpr double kTouchpadPerDoubling = 3 * kWheelNotch;
constexpr double kTouchpadHold = 0.1;
// Tile textures kept on the GPU. Past this, the least recently drawn ones are dropped.
constexpr qint64 kTextureBudgetBytes = 768ll * 1024 * 1024;
// Limits how much is uploaded per frame, so a burst of tiles cannot stall panning.
constexpr qint64 kUploadBytesPerFrame = 64ll * 1024 * 1024;
// From this zoom up, show individual pixels instead of blurring them.
constexpr double kNearestFilterZoom = 2.0;
// How far Magic Kernel Sharp 2013 reaches, in its own units.
constexpr double kResampleSupport = 2.5;

struct TileKey {
    int level = 0;
    int col = 0;
    int row = 0;
    auto operator<=>(const TileKey&) const = default;
};

QVariantMap detailRow(const QString& section, const QString& label, const QString& value) {
    return {{QStringLiteral("section"), section}, {QStringLiteral("label"), label}, {QStringLiteral("value"), value}};
}

// A magnifying glass, as Qt has no such cursor. Light on dark, to show on any image.
QCursor loupeCursor(qreal devicePixelRatio) {
    constexpr int kSize = 24;
    constexpr QPointF kCentre(10, 10);
    QImage image(qCeil(kSize * devicePixelRatio), qCeil(kSize * devicePixelRatio), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(devicePixelRatio);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    const auto draw = [&](const QColor& color, qreal width) {
        painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap));
        painter.drawEllipse(kCentre, 7, 7);
        painter.drawLine(QPointF(15.5, 15.5), QPointF(22, 22));
    };
    draw(Qt::black, 3.5);
    draw(Qt::white, 1.75);
    painter.setPen(QPen(Qt::white, 1.5));
    painter.drawLine(kCentre - QPointF(3.5, 0), kCentre + QPointF(3.5, 0));
    painter.drawLine(kCentre - QPointF(0, 3.5), kCentre + QPointF(0, 3.5));
    painter.end();
    return QCursor(QPixmap::fromImage(image), static_cast<int>(kCentre.x()), static_cast<int>(kCentre.y()));
}

// "12kB 2021-12-24 12:34:56". Touches the file system, so it runs on the worker.
QString summarizeFile(const QString& path) {
    const QFileInfo info(path);
    QString size = QString::fromStdString(shortDataSize(static_cast<std::uint64_t>(info.size())));
    size.remove(QLatin1Char(' '));
    return size + QLatin1Char(' ') + info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

// Touches the file system, so it runs on the worker.
QVariantList describeFile(const QString& path) {
    const QFileInfo info(path);
    const QString section = QStringLiteral("File");
    return {detailRow(section, QStringLiteral("Name"), info.fileName()),
            detailRow(section, QStringLiteral("Folder"), QDir::toNativeSeparators(info.absolutePath())),
            detailRow(section, QStringLiteral("Size"), QLocale().formattedDataSize(info.size())),
            detailRow(section, QStringLiteral("Modified"), info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))};
}

struct TextureEntry {
    std::unique_ptr<QSGTexture> texture;
    qint64 bytes = 0;
    std::uint64_t lastDrawnFrame = 0;
};

// Owns the textures, so that they live and die with the scene graph.
class ImageRootNode final : public QSGTransformNode {
public:
    std::uint64_t document = 0;
    std::map<TileKey, TextureEntry> textures;
    std::vector<TileKey> drawn;
    QSGTexture::Filtering filtering = QSGTexture::Linear;
    qint64 textureBytes = 0;
    std::uint64_t frame = 0;

    void clearTextures() {
        removeTileNodes();
        drawn.clear();
        textures.clear();
        textureBytes = 0;
    }

    void removeTileNodes() {
        while (QSGNode* child = firstChild()) {
            delete child;
        }
    }
};

}  // namespace

struct ImageView::Impl {
    explicit Impl(ImageView* view) : q(view) {}

    ImageView* q;
    WorkQueue& queue = WorkQueue::global();
    WorkQueue::GroupId group = queue.createGroup();

    QString path;
    Status status = Status::Null;
    QString error;
    std::uint64_t latestRequest = 0;

    // The file and its page, which the edits, if any, apply to.
    std::shared_ptr<ImageSource> fileSource;
    int filePage = 0;
    std::optional<Document> edit;
    std::shared_ptr<ImageSource> editSource;  // the edited page as it is now, shown or about to be

    // The displayed document: the page of the file, or the edited page. It stays until a newly
    // requested one is ready to show.
    std::uint64_t document = 0;
    std::shared_ptr<ImageSource> source;
    // Everything below describes the displayed document, and is set anew for each one.
    int page = 0;  // of source, which is always 0 for an edited page
    bool autoContrast = false;
    bool keepAutoContrast = false;  // a setting, not of the document
    bool wideTiles = false;  // half-float textures, as tileImage makes them
    DisplayRange autoDisplayRange;
    DisplayRange displayRange;
    std::shared_ptr<const ColorTransform> colorTransform;
    Backdrop backdrop;  // its x and y are set for each tile

    // The backdrop for a tile, its squares continuing those of the tiles before it.
    Backdrop backdropFor(const TileKey& key) const {
        const LevelInfo& level = levels()[static_cast<std::size_t>(key.level)];
        Backdrop placed = backdrop;
        placed.x = key.col * level.tileSize.width;
        placed.y = key.row * level.tileSize.height;
        return placed;
    }
    std::vector<std::pair<TileKey, ImageBuffer>> preview;
    bool previewIsRough = false;  // a likeness of the level, to be replaced by the level itself
    Size fullSize;
    Affine imageToDisplay;
    Viewport viewport;
    QPointF lastDragPosition;
    QVariantList fileDetails;
    QVariantList details;
    QString fileSummary;
    QString summary;
    QString cameraSummary;
    bool wheelNavigates = false;
    int wheelTurned = 0;  // towards a whole notch, as a touchpad sends small steps
    int zoomTurned = 0;   // the same, towards a zoom step
    // Where a touchpad's swipe has taken the zoom, as if it had no stops, and the zoom it then
    // showed: when the zoom has since changed otherwise, the swipe starts again from there.
    double touchpadFree = 1;
    double touchpadShown = 0;

    // While the button is held on an image that fits, it shows at 100% around that place.
    struct Loupe {
        Viewport::State returnTo;
        QString path;
        int page = 0;
    };
    std::optional<Loupe> loupe;
    bool pressed = false;
    enum class CursorKind { Arrow, Loupe, OpenHand, ClosedHand };
    CursorKind cursorKind = CursorKind::Arrow;

    bool resampling = false;
    ResampleGeometry resample;  // inactive unless it is in use

    bool histogramEnabled = false;
    bool histogramValid = false;    // the histogram below belongs to the current page
    bool histogramPending = false;  // a worker is computing it
    std::uint64_t histogramRequest = 0;
    std::shared_ptr<const Histogram> histogram;

    std::map<TileKey, WorkQueue::TaskId> inFlight;
    std::map<TileKey, QImage> pendingUpload;
    std::set<TileKey> uploaded;
    std::set<TileKey> failed;
    std::set<TileKey> rough;  // shown, but still to be read
    std::uint64_t probeRequest = 0;
    // A reduced copy of the image as shown, ready to display, for straightening.
    ImageBuffer straightenBase;
    ImageBuffer drawBase;  // the picture at full size and upright, as stored, while annotating
    std::uint64_t drawRequest = 0;
    double straightenBaseScale = 1;  // its pixels per pixel of the image
    std::uint64_t straightenRequest = 0;

    // Colour adjustment: the cube loaded for it, and the edit the shader shows, if any.
    std::shared_ptr<const ColorCube> colorCube;
    QString colorCubeName;
    std::optional<ColorEdit> colorPreview;
    QImage colorPreviewLut;

    // Comparing: the view that follows this one's zoom and scroll, and back.
    QPointer<ImageView> linked;
    bool following = false;  // taking the linked view's state, so not to send it back

    // The linked view, when it shows a picture of the same size as this one.
    Impl* matchingLink() const {
        if (!linked || !source || status != Status::Ready) {
            return nullptr;
        }
        Impl& other = *linked->m_impl;
        return other.source && other.status == Status::Ready && other.shownSize() == shownSize() ? &other : nullptr;
    }

    void follow(const Viewport::State& state) {
        const Viewport::State mine = viewport.state();
        if (mine.fitMode == state.fitMode && mine.zoom == state.zoom && mine.offsetX == state.offsetX &&
            mine.offsetY == state.offsetY) {
            return;
        }
        following = true;
        viewport.restore(state);
        viewChanged();
        following = false;
    }

    // The same edit for previewing and applying.
    ColorEdit colorEditFor(const QVariantMap& settings) const {
        const auto value = [&](const char* key, double fallback) {
            return settings.value(QLatin1StringView(key), fallback).toDouble();
        };
        ColorEdit edit;
        edit.tone = levelsCurve(value("black", 0), value("white", 1), value("gamma", 1), value("outBlack", 0), value("outWhite", 1));
        edit.lightness = lightnessCurve(value("brightness", 0), value("contrast", 1), value("lightness", 1));
        edit.chroma = value("saturation", 1);
        edit.gamutMap = settings.value(QStringLiteral("gamutMap"), true).toBool();
        edit.cube = colorCube;
        return edit;
    }

    void setColorPreview(std::optional<ColorEdit> edit) {
        if (edit == colorPreview) {
            return;
        }
        const bool newLut = !edit || !colorPreview || edit->tone != colorPreview->tone ||
                            edit->lightness != colorPreview->lightness || edit->cube != colorPreview->cube;
        colorPreview = std::move(edit);
        if (newLut) {
            colorPreviewLut = colorPreview ? colorLutImage(*colorPreview) : QImage();
        }
        emit q->colorPreviewChanged();
    }

    Size shownSize() const { return source ? displaySize(fullSize, subimage().orientation) : Size{}; }

    // In pixels of the image as shown, from the 35mm equivalent, which refers to the picture
    // the camera took: the file's page before any edit, scaled by resizing since.
    double focalPixelsFor(double focal35mm) const {
        if (!fileSource) {
            return 1;
        }
        const SubimageInfo& original = fileSource->subimages()[static_cast<std::size_t>(filePage)];
        const double focal = focalPixels(displaySize(original.levels.front().size, original.orientation),
                                         focal35mm > 0 ? focal35mm : kDefaultFocal35mm);
        return focal * (edit ? edit->scale() : 1.0);
    }

    double devicePixelRatio() const {
        return q->window() ? q->window()->effectiveDevicePixelRatio() : 1.0;
    }

    const SubimageInfo& subimage() const { return source->subimages()[static_cast<std::size_t>(page)]; }

    std::span<const LevelInfo> levels() const { return subimage().levels; }

    void setStatus(Status newStatus, const QString& message = {}) {
        status = newStatus;
        error = message;
        emit q->statusChanged();
    }

    void updateViewSize() {
        const double dpr = devicePixelRatio();
        viewport.setViewSize(q->width() * dpr, q->height() * dpr);
        viewChanged();
    }

    bool gpuAvailable() const {
        return q->window() && q->window()->rendererInterface()->graphicsApi() != QSGRendererInterface::Software;
    }

    double levelScale(int level) const {
        return static_cast<double>(levels()[static_cast<std::size_t>(level)].size.width) / fullSize.width;
    }

    // The kernel handles any reduction and wants the pixels; bilinear filtering does not.
    int targetLevel() const {
        return resampling && gpuAvailable() ? chooseLevel(levels(), viewport.zoom()) : chooseBilinearLevel(levels(), viewport.zoom());
    }

    void updateResampling() {
        resample = {};
        if (resampling && source && gpuAvailable()) {
            const int level = targetLevel();
            resample = resampleGeometry(viewport, displaySize(fullSize, subimage().orientation), levelScale(level), kResampleSupport);
        }
        emit q->resampleChanged();
    }

    bool canUseLoupe() const {
        return source && viewport.zoom() < 0.999 && viewport.showsWholeImage();
    }

    void updateCursor() {
        CursorKind kind = CursorKind::Arrow;
        if (loupe || canUseLoupe()) {
            kind = CursorKind::Loupe;
        } else if (source && !viewport.showsWholeImage()) {
            kind = pressed ? CursorKind::ClosedHand : CursorKind::OpenHand;
        }
        if (kind == cursorKind) {
            return;
        }
        cursorKind = kind;
        switch (kind) {
        case CursorKind::Arrow: q->unsetCursor(); break;
        case CursorKind::Loupe: q->setCursor(loupeCursor(q->window() ? q->window()->devicePixelRatio() : 1.0)); break;
        case CursorKind::OpenHand: q->setCursor(Qt::OpenHandCursor); break;
        case CursorKind::ClosedHand: q->setCursor(Qt::ClosedHandCursor); break;
        }
    }

    void startLoupe(QPointF position) {
        const double dpr = devicePixelRatio();
        loupe = Loupe{viewport.state(), path, page};
        viewport.setFitMode(FitMode::Manual);
        viewport.zoomBy(1 / viewport.zoom(), position.x() * dpr, position.y() * dpr);
    }

    void endPress() {
        pressed = false;
        if (loupe) {
            // Another image may have come by key in the meantime, with a view of its own.
            if (loupe->path == path && loupe->page == page) {
                viewport.restore(loupe->returnTo);
            }
            loupe.reset();
        }
        viewChanged();
    }

    void viewChanged() {
        updateResampling();
        updateCursor();
        emit q->zoomChanged();
        if (source) {
            requestVisibleTiles();
        }
        q->update();
        if (Impl* other = following ? nullptr : matchingLink()) {
            other->follow(viewport.state());
        }
    }

    std::vector<TileKey> visibleTiles(int level, const RectD& storedRect) const {
        const LevelInfo& info = levels()[static_cast<std::size_t>(level)];
        const TileRange range = tilesInRect(info, fullSize, storedRect);
        std::vector<TileKey> tiles;
        for (int row = range.firstRow; row <= range.lastRow; ++row) {
            for (int col = range.firstCol; col <= range.lastCol; ++col) {
                tiles.push_back({level, col, row});
            }
        }
        return tiles;
    }

    RectD visibleStoredRect() const {
        RectD visible = viewport.visibleRect();
        if (resample.active) {  // the kernel also reads what lies just outside the view
            const Size image = displaySize(fullSize, subimage().orientation);
            const double right = std::min<double>(image.width, visible.x + visible.width + resample.marginPixels);
            const double bottom = std::min<double>(image.height, visible.y + visible.height + resample.marginPixels);
            visible.x = std::max(0.0, visible.x - resample.marginPixels);
            visible.y = std::max(0.0, visible.y - resample.marginPixels);
            visible.width = right - visible.x;
            visible.height = bottom - visible.y;
        }
        return imageToDisplay.inverse().mapRect(visible);
    }

    void requestVisibleTiles() {
        const int level = targetLevel();
        const std::vector<TileKey> wanted = visibleTiles(level, visibleStoredRect());

        // Drop queued requests that scrolled out of view or belong to another zoom.
        for (auto it = inFlight.begin(); it != inFlight.end();) {
            if (std::ranges::find(wanted, it->first) == wanted.end()) {
                queue.cancel(it->second);
                it = inFlight.erase(it);
            } else {
                ++it;
            }
        }
        // From the centre outwards, so what the user is looking at appears first.
        const auto distance = [&](const TileKey& key) {
            const RectD rect = tileRect(levels()[static_cast<std::size_t>(key.level)], fullSize, key.col, key.row);
            const RectD visible = visibleStoredRect();
            return std::abs(rect.x + rect.width / 2 - visible.x - visible.width / 2) +
                   std::abs(rect.y + rect.height / 2 - visible.y - visible.height / 2);
        };
        std::vector<TileKey> ordered = wanted;
        std::ranges::sort(ordered, {}, distance);
        for (const TileKey& key : ordered) {
            const bool missing = rough.contains(key) || (!uploaded.contains(key) && !pendingUpload.contains(key));
            if (missing && !inFlight.contains(key) && !failed.contains(key)) {
                requestTile(key);
            }
        }
    }

    void requestTile(const TileKey& key) {
        const std::uint64_t doc = document;
        const auto id = queue.submit(group, kTilePriority,
                                     [this, source = source, key, doc, page = page, range = displayRange, color = colorTransform, wide = wideTiles,
                                      under = backdropFor(key)](std::stop_token stop) {
            const Result<ImageBuffer> read = source->readTile({page, key.level, key.col, key.row}, stop);
            Result<QImage> tile = std::unexpected(read ? std::string() : read.error());
            if (read) {
                try {
                    tile = tileImage(*read, range, color.get(), wide, under);
                } catch (const std::bad_alloc&) {
                    tile = std::unexpected("not enough memory for tile");
                }
            }
            QMetaObject::invokeMethod(q, [this, key, doc, tile = std::move(tile)] {
                onTileRead(key, doc, tile);
            });
        });
        inFlight.emplace(key, id);
    }

    void onTileRead(const TileKey& key, std::uint64_t doc, const Result<QImage>& tile) {
        if (doc != document) {
            return;
        }
        inFlight.erase(key);
        if (tile) {
            rough.erase(key);
            pendingUpload.insert_or_assign(key, *tile);
            q->update();
        } else if (tile.error() != "cancelled") {
            qWarning() << "Cannot read tile of" << path << ":" << QString::fromStdString(tile.error());
            failed.insert(key);
        }
    }

    // What the worker hands over: a page with its smallest level already decoded.
    struct Loaded {
        std::shared_ptr<ImageSource> source;
        int page = 0;
        std::vector<std::pair<TileKey, ImageBuffer>> preview;
        bool previewIsRough = false;
        DisplayRange autoRange;
        QVariantList fileDetails;  // only when a file was opened, not for another page
        QString fileSummary;
        enum class Kind { File, Page, Edit } kind = Kind::File;
    };

    static Result<Loaded> loadPreview(std::shared_ptr<ImageSource> source, int page, const std::stop_token& stop) {
        const SubimageInfo& info = source->subimages()[static_cast<std::size_t>(page)];
        Loaded loaded{source, page, {}, false, {}, {}, {}};
        const int coarsest = static_cast<int>(info.levels.size()) - 1;
        const LevelInfo& level = info.levels.back();
        const int cols = (level.size.width + level.tileSize.width - 1) / level.tileSize.width;
        const int rows = (level.size.height + level.tileSize.height - 1) / level.tileSize.height;
        // A level the viewer makes needs the whole image read first, while a likeness of it,
        // such as a JPEG decoded at 1/8, shows something sooner.
        std::optional<ImageBuffer> likeness;
        if (level.generated) {
            if (auto quick = source->roughLevel(page, level.size, stop); quick && quick->size() == level.size) {
                likeness = *quick;
                loaded.previewIsRough = true;
            }
        }
        std::vector<ImageBuffer> tiles;
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                auto tile = likeness ? Result<ImageBuffer>(likeness->view(col * level.tileSize.width, row * level.tileSize.height,
                                                                          std::min(level.tileSize.width, level.size.width - col * level.tileSize.width),
                                                                          std::min(level.tileSize.height, level.size.height - row * level.tileSize.height)))
                                     : source->readTile({page, coarsest, col, row}, stop);
                if (!tile) {
                    return std::unexpected(tile.error());
                }
                tiles.push_back(*tile);
                loaded.preview.emplace_back(TileKey{coarsest, col, row}, *tile);
            }
        }
        // The range is of what is shown, which for now is the picture flattened.
        std::ranges::transform(tiles, tiles.begin(), [](const ImageBuffer& tile) { return flattened(tile); });
        loaded.autoRange = autoRange(tiles);
        displayTransform(info, ColorSettings::target());  // cached, so showing the page does not wait for it
        return loaded;
    }

    void open(const QString& newPath) {
        const std::uint64_t request = ++latestRequest;
        setColorPreview(std::nullopt);
        queue.cancelGroup(group);
        inFlight.clear();

        if (newPath.isEmpty()) {
            closeDocument();
            setStatus(Status::Null);
            return;
        }
        setStatus(Status::Loading);

        histogramPending = false;
        queue.submit(group, kOpenPriority,
                     [this, newPath, file = QFileInfo(newPath).filesystemFilePath(), request](std::stop_token stop) {
            Result<Loaded> loaded = std::unexpected("cancelled");
            try {
                auto source = openImageSource(file, ReadPriority::Foreground, stop);
                loaded = source ? loadPreview(*source, 0, stop) : Result<Loaded>(std::unexpected(source.error()));
                if (loaded) {
                    loaded->fileDetails = describeFile(newPath);
                    loaded->fileSummary = summarizeFile(newPath);
                }
            } catch (const std::bad_alloc&) {
                loaded = std::unexpected("not enough memory to open image");
            }
            QMetaObject::invokeMethod(q, [this, request, loaded = std::move(loaded)] {
                onLoaded(request, loaded);
            });
        });
    }

    void showPage(int newPage) {
        if (!fileSource || newPage == filePage || newPage < 0 || newPage >= std::ssize(fileSource->subimages())) {
            return;
        }
        loadFilePage(newPage);
    }

    // Edits to the page shown are dropped.
    void loadFilePage(int newPage) {
        load(fileSource, newPage, Loaded::Kind::Page);
    }

    void showEdit() {
        editSource = edit->source();
        load(editSource, 0, Loaded::Kind::Edit);
        emit q->editChanged();  // undo and redo are up to date before the picture
    }

    void load(std::shared_ptr<ImageSource> from, int newPage, Loaded::Kind kind) {
        const std::uint64_t request = ++latestRequest;
        queue.cancelGroup(group);
        inFlight.clear();
        histogramPending = false;
        setStatus(Status::Loading);
        queue.submit(group, kOpenPriority, [this, from = std::move(from), newPage, kind, request](std::stop_token stop) {
            Result<Loaded> loaded = std::unexpected("cancelled");
            try {
                loaded = loadPreview(from, newPage, stop);
                if (loaded) {
                    loaded->kind = kind;
                }
            } catch (const std::bad_alloc&) {
                loaded = std::unexpected("not enough memory to show the image");
            }
            QMetaObject::invokeMethod(q, [this, request, loaded = std::move(loaded)] {
                onLoaded(request, loaded);
            });
        });
    }

    void onLoaded(std::uint64_t request, const Result<Loaded>& loaded) {
        if (request != latestRequest) {
            return;
        }
        // A colour preview belongs to the picture it was drawn over. Applying one keeps it up
        // until the edited picture takes its place, so the change does not flash.
        setColorPreview(std::nullopt);
        if (!loaded) {
            closeDocument();
            setStatus(Status::Error, QString::fromStdString(loaded.error()));
            return;
        }

        source = loaded->source;
        page = loaded->page;
        switch (loaded->kind) {
        case Loaded::Kind::File:
            fileSource = source;
            filePage = page;
            edit.reset();
            editSource.reset();  // it keeps the decoded picture
            break;
        case Loaded::Kind::Page:
            filePage = page;
            edit.reset();
            editSource.reset();  // it keeps the decoded picture
            break;
        case Loaded::Kind::Edit:
            break;
        }
        emit q->editChanged();
        preview = loaded->preview;
        previewIsRough = loaded->previewIsRough;
        autoDisplayRange = loaded->autoRange;
        const SubimageInfo& info = subimage();
        // 8-bit images have nothing to map, and are shown as they are.
        autoContrast = (keepAutoContrast && info.format != PixelFormat::Bgrx8) || usesAutoRangeByDefault(info.format);
        fullSize = info.levels.front().size;
        imageToDisplay = displayFromStored(fullSize, info.orientation);

        if (viewport.fitMode() == FitMode::Manual) {
            viewport.setFitMode(FitMode::ShrinkToFit);
        }
        viewport.setImageSize(displaySize(fullSize, info.orientation));
        // Joining a comparison takes the other picture's zoom and place rather than refitting it.
        if (const Impl* other = matchingLink()) {
            viewport.restore(other->viewport.state());
        }
        startDisplaying();
        setStatus(Status::Ready);
        emit q->pageChanged();
        viewChanged();

        if (!loaded->fileDetails.isEmpty()) {
            fileDetails = loaded->fileDetails;
            fileSummary = loaded->fileSummary;
        }
        updateDetails();
        histogramValid = false;
        if (histogramEnabled) {
            requestHistogram();  // the old one stays up until this arrives, without a flicker
        } else if (histogram) {
            histogram.reset();
            emit q->histogramChanged();
        }
    }

    void updateDetails() {
        details = fileDetails;
        summary.clear();
        cameraSummary.clear();
        if (source) {
            // Keep the file date on its own metadata line rather than burying it after
            // the dimensions, format and bit depth in the compact preview footer.
            summary = QString::fromStdString(imageSummary(subimage())) + QLatin1Char('\n') + fileSummary;
            cameraSummary = QString::fromStdString(iv::cameraSummary(subimage().exif));
            const auto pages = std::ssize(fileSource->subimages());
            if (pages > 1) {
                details.append(detailRow(QStringLiteral("File"), QStringLiteral("Page"), QStringLiteral("%1 of %2").arg(filePage + 1).arg(pages)));
            }
            for (const DetailRow& row : describeImage(subimage())) {
                QVariantMap map = detailRow(QString::fromStdString(row.section), QString::fromStdString(row.label),
                                            QString::fromStdString(row.value));
                if (!row.link.empty()) {
                    map.insert(QStringLiteral("link"), QString::fromStdString(row.link));
                }
                details.append(map);
            }
        }
        emit q->detailsChanged();
    }

    void requestHistogram() {
        if (!histogramEnabled || !source || histogramValid || histogramPending) {
            return;
        }
        histogramPending = true;
        const std::uint64_t request = ++histogramRequest;
        queue.submit(group, kHistogramPriority, [this, source = source, page = page, request](std::stop_token stop) {
            Result<std::shared_ptr<const Histogram>> result = std::unexpected("cancelled");
            try {
                auto computed = computeHistogram(*source, page, stop);
                if (computed) {
                    result = std::make_shared<const Histogram>(std::move(*computed));
                } else {
                    result = std::unexpected(computed.error());
                }
            } catch (const std::bad_alloc&) {
                result = std::unexpected("not enough memory for histogram");
            }
            QMetaObject::invokeMethod(q, [this, request, result = std::move(result)] {
                if (request != histogramRequest) {
                    return;
                }
                histogramPending = false;
                if (!result && result.error() == "cancelled") {
                    return;
                }
                histogramValid = true;
                histogram = result ? *result : nullptr;
                emit q->histogramChanged();
            });
        });
    }

    // Starts drawing the current page with the current display range, from the preview.
    void startDisplaying() {
        ++document;
        const PixelFormat format = subimage().format;
        displayRange = autoContrast ? autoDisplayRange : fullRange(format);
        colorTransform = displayTransform(subimage(), ColorSettings::target());
        const QQuickWindow* window = q->window();
        wideTiles = !window || window->rendererInterface()->graphicsApi() != QSGRendererInterface::Software;
        inFlight.clear();
        uploaded.clear();
        failed.clear();
        pendingUpload.clear();
        rough.clear();
        for (const auto& [key, tile] : preview) {
            pendingUpload.emplace(key, tileImage(tile, displayRange, colorTransform.get(), wideTiles, backdropFor(key)));
            if (previewIsRough) {
                rough.insert(key);
            }
        }
    }

    void setAutoContrast(bool enabled) {
        if (!source || enabled == autoContrast) {
            return;
        }
        autoContrast = enabled;
        redisplay();
    }

    void colorSettingsChanged() {
        if (status == Status::Loading && edit) {
            showEdit();
        } else if (status == Status::Loading) {
            open(path);  // what is being loaded would be cancelled along with the tiles
        } else if (source) {
            redisplay();
        }
    }

    void redisplay() {
        queue.cancelGroup(group);
        histogramPending = false;
        startDisplaying();
        emit q->pageChanged();
        viewChanged();
        requestHistogram();  // cancelling the group may have stopped one
    }

    void closeDocument() {
        ++document;
        fileSource.reset();
        filePage = 0;
        edit.reset();
        editSource.reset();
        emit q->editChanged();
        source.reset();
        colorTransform.reset();
        page = 0;
        autoContrast = false;
        preview.clear();
        previewIsRough = false;
        rough.clear();
        inFlight.clear();
        uploaded.clear();
        failed.clear();
        pendingUpload.clear();
        viewport.setImageSize({});
        fileDetails.clear();
        details.clear();
        fileSummary.clear();
        summary.clear();
        cameraSummary.clear();
        updateCursor();
        histogramValid = false;
        histogramPending = false;
        ++histogramRequest;
        histogram.reset();
        emit q->pageChanged();
        emit q->zoomChanged();
        emit q->detailsChanged();
        emit q->histogramChanged();
        q->update();
    }

    // The steps below run on the render thread while the GUI thread is blocked, so they
    // may touch this object, as Qt documents for updatePaintNode().

    void uploadPending(ImageRootNode& root) {
        qint64 budget = kUploadBytesPerFrame;
        for (auto it = pendingUpload.begin(); it != pendingUpload.end() && budget > 0;) {
            const qint64 bytes = it->second.sizeInBytes();
            budget -= bytes;
            std::unique_ptr<QSGTexture> texture(q->window()->createTextureFromImage(it->second));
            if (const auto old = root.textures.find(it->first); old != root.textures.end()) {
                // A rough tile replaced by the real one: no node may keep the old texture.
                if (std::ranges::find(root.drawn, it->first) != root.drawn.end()) {
                    root.removeTileNodes();
                    root.drawn.clear();
                }
                root.textureBytes -= old->second.bytes;
            }
            root.textures.insert_or_assign(it->first, TextureEntry{std::move(texture), bytes, root.frame});
            root.textureBytes += bytes;
            uploaded.insert(it->first);
            it = pendingUpload.erase(it);
        }
        if (!pendingUpload.empty()) {
            q->update();
        }
    }

    // Coarser levels go underneath as a placeholder until the target level is complete.
    std::vector<TileKey> tilesToDraw(const ImageRootNode& root) const {
        const int target = targetLevel();
        const RectD visible = visibleStoredRect();
        const std::vector<TileKey> targetTiles = visibleTiles(target, visible);
        const bool complete = std::ranges::all_of(
            targetTiles, [&](const TileKey& key) { return root.textures.contains(key); });
        if (complete) {
            return targetTiles;
        }

        std::vector<TileKey> result;
        for (int level = static_cast<int>(levels().size()) - 1; level >= target; --level) {
            for (const TileKey& key : visibleTiles(level, visible)) {
                if (root.textures.contains(key)) {
                    result.push_back(key);
                }
            }
        }
        return result;
    }

    void syncTileNodes(ImageRootNode& root, const std::vector<TileKey>& tiles, QSGTexture::Filtering filtering) {
        if (tiles == root.drawn && filtering == root.filtering) {
            return;
        }
        root.removeTileNodes();
        for (const TileKey& key : tiles) {
            const LevelInfo& level = levels()[static_cast<std::size_t>(key.level)];
            QSGTexture* texture = root.textures.at(key).texture.get();
            const RectD rect = tileRect(level, fullSize, key.col, key.row);
            auto* node = new QSGSimpleTextureNode;
            node->setOwnsTexture(false);
            node->setTexture(texture);
            // Going through the kernel the target level maps texel to texel, and must stay
            // exact, while what stands in for it until it arrives is enlarged as usual.
            node->setFiltering(resample.active && key.level != targetLevel() ? QSGTexture::Linear : filtering);
            node->setRect(rect.x, rect.y, rect.width, rect.height);
            root.appendChildNode(node);
        }
        root.drawn = tiles;
        root.filtering = filtering;
    }

    void evictTextures(ImageRootNode& root) {
        const int coarsest = static_cast<int>(levels().size()) - 1;
        while (root.textureBytes > kTextureBudgetBytes) {
            auto oldest = root.textures.end();
            for (auto it = root.textures.begin(); it != root.textures.end(); ++it) {
                // The coarsest level is the placeholder and always stays.
                if (it->first.level != coarsest && it->second.lastDrawnFrame != root.frame &&
                    (oldest == root.textures.end() || it->second.lastDrawnFrame < oldest->second.lastDrawnFrame)) {
                    oldest = it;
                }
            }
            if (oldest == root.textures.end()) {
                return;
            }
            root.textureBytes -= oldest->second.bytes;
            uploaded.erase(oldest->first);
            root.textures.erase(oldest);
        }
    }

    QMatrix4x4 storedToItemMatrix() const {
        if (resample.active) {
            // Item coordinates are stretched over the texture, so one texel per level pixel
            // here means this.
            const double unitX = q->width() / resample.textureWidth;
            const double unitY = q->height() / resample.textureHeight;
            const Affine& m = imageToDisplay;
            return QMatrix4x4(static_cast<float>(m.a * resample.scale * unitX), static_cast<float>(m.c * resample.scale * unitX), 0,
                              static_cast<float>((m.tx * resample.scale + resample.translateX) * unitX),
                              static_cast<float>(m.b * resample.scale * unitY), static_cast<float>(m.d * resample.scale * unitY), 0,
                              static_cast<float>((m.ty * resample.scale + resample.translateY) * unitY),
                              0, 0, 1, 0,
                              0, 0, 0, 1);
        }
        const double scale = viewport.zoom() / devicePixelRatio();
        const double offsetX = viewport.offsetX() / devicePixelRatio();
        const double offsetY = viewport.offsetY() / devicePixelRatio();
        const Affine& m = imageToDisplay;
        return QMatrix4x4(static_cast<float>(m.a * scale), static_cast<float>(m.c * scale), 0,
                          static_cast<float>(m.tx * scale + offsetX),
                          static_cast<float>(m.b * scale), static_cast<float>(m.d * scale), 0,
                          static_cast<float>(m.ty * scale + offsetY),
                          0, 0, 1, 0,
                          0, 0, 0, 1);
    }
};

ImageView::ImageView(QQuickItem* parent) : QQuickItem(parent), m_impl(std::make_unique<Impl>(this)) {
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton);
    connect(ColorSettings::instance(), &ColorSettings::changed, this, [this] { m_impl->colorSettingsChanged(); });
}

ImageView::~ImageView() {
    // Running tasks post their results to this object, so they must finish first.
    m_impl->queue.cancelGroup(m_impl->group);
    m_impl->queue.waitGroup(m_impl->group);
}

QString ImageView::source() const { return m_impl->path; }

void ImageView::setSource(const QString& path) {
    if (path == m_impl->path) {
        return;
    }
    m_impl->path = path;
    emit sourceChanged();
    m_impl->open(path);
}

ImageView::Status ImageView::status() const { return m_impl->status; }
QString ImageView::errorString() const { return m_impl->error; }

int ImageView::imageWidth() const {
    return m_impl->source ? displaySize(m_impl->fullSize, m_impl->subimage().orientation).width : 0;
}

int ImageView::imageHeight() const {
    return m_impl->source ? displaySize(m_impl->fullSize, m_impl->subimage().orientation).height : 0;
}

int ImageView::page() const { return m_impl->filePage; }
void ImageView::setPage(int page) { m_impl->showPage(page); }
int ImageView::pageCount() const { return m_impl->fileSource ? static_cast<int>(m_impl->fileSource->subimages().size()) : 0; }

bool ImageView::contrastAdjustable() const {
    return m_impl->source && m_impl->subimage().format != PixelFormat::Bgrx8;
}

bool ImageView::highPrecision() const {
    if (!m_impl->source || !m_impl->wideTiles) {
        return false;
    }
    const PixelFormat format = m_impl->subimage().format;
    return format != PixelFormat::Bgrx8 && format != PixelFormat::Gray8;
}

bool ImageView::autoContrast() const { return m_impl->autoContrast; }
void ImageView::setAutoContrast(bool enabled) { m_impl->setAutoContrast(enabled); }
bool ImageView::keepAutoContrast() const { return m_impl->keepAutoContrast; }

void ImageView::setKeepAutoContrast(bool keep) {
    if (keep == m_impl->keepAutoContrast) {
        return;
    }
    m_impl->keepAutoContrast = keep;
    emit keepAutoContrastChanged();
    if (keep && contrastAdjustable()) {
        m_impl->setAutoContrast(true);
    }
}

namespace {

QColor toQColor(const Rgba8& c) { return QColor(c.r, c.g, c.b); }

Rgba8 fromQColor(const QColor& c) {
    const QColor rgb = c.toRgb();
    return {static_cast<std::uint8_t>(rgb.red()), static_cast<std::uint8_t>(rgb.green()), static_cast<std::uint8_t>(rgb.blue()), 255};
}

}  // namespace

QColor ImageView::backdropFirst() const { return toQColor(m_impl->backdrop.first); }

QColor ImageView::backdropSecond() const { return toQColor(m_impl->backdrop.second); }

void ImageView::setBackdropFirst(const QColor& colour) {
    if (fromQColor(colour) != m_impl->backdrop.first) {
        m_impl->backdrop.first = fromQColor(colour);
        emit backdropChanged();
        m_impl->colorSettingsChanged();  // the tiles are made again on it
    }
}

void ImageView::setBackdropSecond(const QColor& colour) {
    if (fromQColor(colour) != m_impl->backdrop.second) {
        m_impl->backdrop.second = fromQColor(colour);
        emit backdropChanged();
        m_impl->colorSettingsChanged();
    }
}

double ImageView::zoom() const { return m_impl->viewport.zoom(); }

QRectF ImageView::visibleArea() const {
    const RectD area = m_impl->viewport.visibleFraction();
    return {area.x, area.y, area.width, area.height};
}

QVariantList ImageView::details() const { return m_impl->details; }
QString ImageView::summary() const { return m_impl->summary; }
QString ImageView::cameraSummary() const { return m_impl->cameraSummary; }
bool ImageView::wheelNavigates() const { return m_impl->wheelNavigates; }

void ImageView::setWheelNavigates(bool navigates) {
    if (navigates != m_impl->wheelNavigates) {
        m_impl->wheelNavigates = navigates;
        emit wheelNavigatesChanged();
    }
}

bool ImageView::histogramEnabled() const { return m_impl->histogramEnabled; }

void ImageView::setHistogramEnabled(bool enabled) {
    if (enabled == m_impl->histogramEnabled) {
        return;
    }
    m_impl->histogramEnabled = enabled;
    emit histogramChanged();
    m_impl->requestHistogram();
}

QVariant ImageView::histogram() const {
    return m_impl->histogram ? QVariant::fromValue(m_impl->histogram) : QVariant();
}

bool ImageView::resampling() const { return m_impl->resampling; }

void ImageView::setResampling(bool enabled) {
    if (enabled == m_impl->resampling) {
        return;
    }
    m_impl->resampling = enabled;
    m_impl->viewChanged();
}

bool ImageView::resampleActive() const { return m_impl->resample.active; }

QSize ImageView::resampleTextureSize() const { return {m_impl->resample.textureWidth, m_impl->resample.textureHeight}; }

QPointF ImageView::resamplePhase() const { return {m_impl->resample.phaseX, m_impl->resample.phaseY}; }

QRectF ImageView::resampleBounds() const {
    const RectD& r = m_impl->resample.imageTexels;
    return {r.x, r.y, r.width, r.height};
}

QRectF ImageView::resampleImageRect() const {
    const auto& vp = m_impl->viewport;
    if (!m_impl->resample.active || vp.viewWidth() <= 0 || vp.viewHeight() <= 0) {
        return {};
    }
    const Size image = displaySize(m_impl->fullSize, m_impl->subimage().orientation);
    return {vp.offsetX() / vp.viewWidth(), vp.offsetY() / vp.viewHeight(), image.width * vp.zoom() / vp.viewWidth(),
            image.height * vp.zoom() / vp.viewHeight()};
}

double ImageView::resampleRatio() const { return m_impl->resample.ratio; }
double ImageView::resampleSpacing() const { return m_impl->resample.spacing; }
double ImageView::resampleDevicePixelRatio() const { return m_impl->devicePixelRatio(); }

bool ImageView::editable() const {
    return m_impl->fileSource && notEditableReason().isEmpty();
}

QString ImageView::notEditableReason() const {
    const Impl& d = *m_impl;
    if (!d.fileSource) {
        return {};
    }
    const auto allowed = canEdit(d.fileSource->subimages()[static_cast<std::size_t>(d.filePage)]);
    return allowed ? QString() : QString::fromStdString(allowed.error());
}

bool ImageView::modified() const { return m_impl->edit && m_impl->edit->modified(); }
bool ImageView::canUndo() const { return m_impl->edit && m_impl->edit->canUndo(); }
bool ImageView::canRedo() const { return m_impl->edit && m_impl->edit->canRedo(); }

bool ImageView::grayImage() const {
    if (!m_impl->source) {
        return false;
    }
    const PixelFormat format = m_impl->subimage().format;
    return format == PixelFormat::Gray8 || format == PixelFormat::Gray16 || format == PixelFormat::Gray32F;
}

bool ImageView::colorPreviewActive() const { return m_impl->colorPreview.has_value(); }
QImage ImageView::colorPreviewLut() const { return m_impl->colorPreviewLut; }

int ImageView::colorPreviewCubeSize() const {
    const auto& preview = m_impl->colorPreview;
    return preview && preview->cube && !grayImage() ? preview->cube->size : 0;
}

double ImageView::colorPreviewChroma() const {
    const auto& preview = m_impl->colorPreview;
    return preview && !grayImage() ? preview->chroma : 1.0;
}

bool ImageView::colorPreviewTone() const {
    return m_impl->colorPreview && !m_impl->colorPreview->tone.isIdentity();
}

bool ImageView::colorPreviewLightness() const {
    return m_impl->colorPreview && !m_impl->colorPreview->lightness.isIdentity();
}

bool ImageView::colorPreviewGamutMap() const {
    return !m_impl->colorPreview || m_impl->colorPreview->gamutMap;
}

QVector4D ImageView::colorPreviewDomainMin() const {
    const auto& preview = m_impl->colorPreview;
    if (!preview || !preview->cube) {
        return {0, 0, 0, 0};
    }
    const auto& d = preview->cube->domainMin;
    return {d[0], d[1], d[2], 0};
}

QVector4D ImageView::colorPreviewDomainMax() const {
    const auto& preview = m_impl->colorPreview;
    if (!preview || !preview->cube) {
        return {1, 1, 1, 1};
    }
    const auto& d = preview->cube->domainMax;
    return {d[0], d[1], d[2], 1};
}

QString ImageView::colorCubeName() const { return m_impl->colorCubeName; }

ImageView* ImageView::linkedView() const { return m_impl->linked; }

void ImageView::setLinkedView(ImageView* view) {
    if (view == m_impl->linked || view == this) {
        return;
    }
    m_impl->linked = view;
    emit linkedViewChanged();
    // The newly linked view takes this one's zoom and place.
    if (Impl* other = m_impl->matchingLink()) {
        other->follow(m_impl->viewport.state());
    }
}

void ImageView::showPictureOf(ImageView* other, bool original) {
    Impl& d = *m_impl;
    if (!other || other == this) {
        return;
    }
    const Impl& o = *other->m_impl;
    std::shared_ptr<ImageSource> from = original ? o.fileSource : o.source;
    const int page = original ? o.filePage : o.page;
    if (!from) {
        setSource({});
        return;
    }
    d.path = o.path;
    emit sourceChanged();
    d.setColorPreview(std::nullopt);
    d.fileSource = std::move(from);
    d.loadFilePage(page);
}

void ImageView::previewColors(const QVariantMap& settings) {
    Impl& d = *m_impl;
    if (d.status != Status::Ready) {
        return;
    }
    d.setColorPreview(d.colorEditFor(settings));
}

void ImageView::endColorPreview() { m_impl->setColorPreview(std::nullopt); }

void ImageView::adjustColors(const QVariantMap& settings) {
    Impl& d = *m_impl;
    ColorEdit color = d.colorEditFor(settings);
    if (grayImage()) {
        color.chroma = 1;
        color.cube.reset();
    }
    if (!editable() || d.status != Status::Ready || color.isIdentity()) {
        d.setColorPreview(std::nullopt);
        return;
    }
    if (!d.edit) {
        d.edit.emplace(d.fileSource, d.filePage);
    }
    d.edit->apply(std::move(color));
    d.showEdit();
}

QString ImageView::loadColorCube(const QUrl& file) {
    constexpr qint64 kLargestFile = 256LL << 20;
    QFile in(file.isLocalFile() ? file.toLocalFile() : file.toString());
    if (!in.open(QIODevice::ReadOnly)) {
        return QStringLiteral("Cannot open the file: %1.").arg(in.errorString());
    }
    if (in.size() > kLargestFile) {
        return QStringLiteral("The file is too large for a colour table.");
    }
    const QByteArray text = in.readAll();
    auto cube = parseCube(std::string_view(text.constData(), static_cast<std::size_t>(text.size())));
    if (!cube) {
        QString reason = QString::fromStdString(cube.error());
        if (!reason.isEmpty()) {
            reason[0] = reason[0].toUpper();
        }
        return reason + QLatin1Char('.');
    }
    m_impl->colorCube = std::make_shared<const ColorCube>(std::move(*cube));
    m_impl->colorCubeName = QFileInfo(in.fileName()).completeBaseName();
    emit colorPreviewChanged();
    return {};
}

void ImageView::clearColorCube() {
    m_impl->colorCube.reset();
    m_impl->colorCubeName.clear();
    emit colorPreviewChanged();
}

void ImageView::panBy(double dx, double dy) {
    const double dpr = m_impl->devicePixelRatio();
    m_impl->viewport.panBy(dx * dpr, dy * dpr);
    m_impl->viewChanged();
}

QPointF ImageView::imageToItem(QPointF point) const {
    const auto& vp = m_impl->viewport;
    const double dpr = m_impl->devicePixelRatio();
    return {(point.x() * vp.zoom() + vp.offsetX()) / dpr, (point.y() * vp.zoom() + vp.offsetY()) / dpr};
}

QPointF ImageView::itemToImage(QPointF point) const {
    const auto& vp = m_impl->viewport;
    const double dpr = m_impl->devicePixelRatio();
    return {(point.x() * dpr - vp.offsetX()) / vp.zoom(), (point.y() * dpr - vp.offsetY()) / vp.zoom()};
}

void ImageView::crop(const QRect& rect) {
    Impl& d = *m_impl;
    if (!editable() || d.status != Status::Ready || rect.isEmpty()) {
        return;
    }
    if (!d.edit) {
        d.edit.emplace(d.fileSource, d.filePage);
    }
    const Size size = d.edit->size();
    const PixelRect pixels{rect.x(), rect.y(), rect.width(), rect.height()};
    if (pixels.x < 0 || pixels.y < 0 || pixels.right() > size.width || pixels.bottom() > size.height ||
        pixels == PixelRect{0, 0, size.width, size.height}) {
        return;
    }
    d.edit->apply(CropEdit{pixels});
    d.showEdit();
}

QString ImageView::resizeProblem(int width, int height) const {
    const Impl& d = *m_impl;
    if (!editable() || d.status != Status::Ready) {
        return QStringLiteral("The image cannot be edited.");
    }
    const auto ok = d.edit ? d.edit->canResizeTo({width, height}) : Document(d.fileSource, d.filePage).canResizeTo({width, height});
    if (!ok) {
        QString reason = QString::fromStdString(ok.error());
        reason[0] = reason[0].toUpper();
        return reason + QLatin1Char('.');
    }
    return {};
}

namespace {

FrameLook frameLookFrom(const QVariantMap& map) {
    FrameLook look;
    const QVariantList frames = map.value(QStringLiteral("frames")).toList();
    for (qsizetype i = 0; i < std::min<qsizetype>(frames.size(), 3); ++i) {
        const QVariantMap frame = frames[i].toMap();
        const QColor colour = frame.value(QStringLiteral("colour")).value<QColor>().toRgb();
        look.frames[static_cast<std::size_t>(i)] = {
            std::max(0, frame.value(QStringLiteral("width")).toInt()),
            {static_cast<std::uint8_t>(colour.red()), static_cast<std::uint8_t>(colour.green()), static_cast<std::uint8_t>(colour.blue()), 255}};
    }
    look.shadow = std::max(0, map.value(QStringLiteral("shadow")).toInt());
    look.transparent = map.value(QStringLiteral("transparent")).toBool();
    return look;
}

}  // namespace

QSize ImageView::framedSize(const QVariantMap& look) const {
    if (m_impl->status != Status::Ready) {
        return {};
    }
    const Size size = iv::framedSize({imageWidth(), imageHeight()}, frameLookFrom(look));
    return {size.width, size.height};
}

QString ImageView::frameProblem(const QVariantMap& look) const {
    const QSize size = framedSize(look);
    if (size.isValid() && size == QSize(imageWidth(), imageHeight())) {
        return QStringLiteral("Give a frame a width, or the shadow a size.");
    }
    return resizeProblem(size.width(), size.height());
}

void ImageView::frameImage(const QVariantMap& look) {
    Impl& d = *m_impl;
    if (!frameProblem(look).isEmpty()) {
        return;
    }
    if (!d.edit) {
        d.edit.emplace(d.fileSource, d.filePage);
    }
    d.edit->apply(FrameEdit{frameLookFrom(look)});
    d.showEdit();
}

void ImageView::resizeImage(int width, int height, bool fill) {
    Impl& d = *m_impl;
    if (!resizeProblem(width, height).isEmpty()) {
        return;
    }
    if (!d.edit) {
        d.edit.emplace(d.fileSource, d.filePage);
    }
    if (d.edit->size() == Size{width, height}) {
        return;
    }
    d.edit->apply(ResizeEdit{{width, height}, fill});
    d.showEdit();
}

void ImageView::grayscale(int method) {
    Impl& d = *m_impl;
    if (!editable() || d.status != Status::Ready) {
        return;
    }
    if (!d.edit) {
        d.edit.emplace(d.fileSource, d.filePage);
    }
    d.edit->apply(GrayEdit{method == 1 ? GrayMethod::Lightness : GrayMethod::Luma});
    d.showEdit();
}

void ImageView::draw(const std::vector<Shape>& shapes) {
    Impl& d = *m_impl;
    if (!editable() || d.status != Status::Ready || shapes.empty()) {
        return;
    }
    if (!d.edit) {
        d.edit.emplace(d.fileSource, d.filePage);
    }
    d.edit->apply(renderShapes(shapes, d.edit->size(), d.displayRange));
    d.showEdit();
}

void ImageView::prepareDraw() {
    Impl& d = *m_impl;
    d.drawBase = {};
    const std::uint64_t request = ++d.drawRequest;
    if (!d.source || d.status != Status::Ready) {
        return;
    }
    d.queue.submit(d.group, kStraightenPriority,
                   [this, source = d.source, page = d.page, orientation = d.subimage().orientation, request](std::stop_token stop) {
        auto pixels = readLevel(*source, page, 0, stop);
        if (!pixels) {
            return;
        }
        ImageBuffer base;
        try {
            base = applyOrientation(flattened(*pixels), orientation);  // until drawing keeps alpha
        } catch (const std::bad_alloc&) {
            return;
        }
        QMetaObject::invokeMethod(this, [this, base = std::move(base), request] {
            if (request == m_impl->drawRequest) {
                m_impl->drawBase = base;
                emit drawReady();
            }
        });
    });
}

void ImageView::endDraw() {
    ++m_impl->drawRequest;
    m_impl->drawBase = {};
}

QImage ImageView::drawPreview(const Shape& shape, QRect* where) const {
    const Impl& d = *m_impl;
    if (d.drawBase.isNull()) {
        return {};
    }
    const QRect area = shapeArea(shape, d.drawBase.size());
    if (area.isEmpty()) {
        return {};
    }
    *where = area;
    // More pixels than this are reduced first: quick enough to follow the mouse, and a part
    // that large is shown smaller than it is anyway.
    constexpr double kMostPixels = 1.5e6;
    const double scale = std::min(1.0, std::sqrt(kMostPixels / (static_cast<double>(area.width()) * area.height())));
    try {
        ImageBuffer part = d.drawBase.view(area.x(), area.y(), area.width(), area.height());
        Shape local = shape;
        moveBy(local, {-static_cast<double>(area.x()), -static_cast<double>(area.y())});
        if (scale < 1) {
            const Size reduced{std::max(1, static_cast<int>(area.width() * scale)), std::max(1, static_cast<int>(area.height() * scale))};
            const double sx = static_cast<double>(reduced.width) / area.width();
            const double sy = static_cast<double>(reduced.height) / area.height();
            part = shrink(part, reduced);
            for (PointD& p : local.points) {
                p = {p.x * sx, p.y * sy};
            }
            local.style.width = std::max(1.0, local.style.width * sx);
        }
        const DrawEdit edit = renderShapes(std::span(&local, 1), part.size(), d.displayRange);
        for (const DrawEdit::Pixelated& box : edit.pixelated) {
            part = pixelate(part, box.rect, box.block);
        }
        if (!edit.markers.isNull()) {
            part = blendLayer(edit.colour ? toColour(part) : part, edit.markers, edit.x, edit.y, LayerBlend::Multiply, edit.range);
        }
        return toQImage(toDisplay(part, d.displayRange, d.colorTransform.get()));
    } catch (const std::bad_alloc&) {
        return {};
    }
}

void ImageView::undo() {
    if (canUndo()) {
        m_impl->edit->undo();
        m_impl->showEdit();
    }
}

void ImageView::redo() {
    if (canRedo()) {
        m_impl->edit->redo();
        m_impl->showEdit();
    }
}

void ImageView::discardEdits() {
    if (m_impl->edit) {
        m_impl->loadFilePage(m_impl->filePage);
    }
}

QVariant ImageView::editedImage() const {
    const Impl& d = *m_impl;
    return d.edit && !d.edit->edits().empty() ? QVariant::fromValue(d.editSource) : QVariant();
}

void ImageView::markSaved(const QVariant& image) {
    Impl& d = *m_impl;
    if (d.edit && image.value<std::shared_ptr<ImageSource>>() == d.editSource) {
        d.edit->markSaved();
        emit editChanged();
    }
}

void ImageView::probe(QPointF point) {
    Impl& d = *m_impl;
    if (!d.source || d.status != Status::Ready) {
        return;
    }
    const int x = static_cast<int>(std::floor(point.x()));
    const int y = static_cast<int>(std::floor(point.y()));
    const Size shown = displaySize(d.fullSize, d.subimage().orientation);
    if (x < 0 || y < 0 || x >= shown.width || y >= shown.height) {
        return;
    }
    // The centre of the pixel as shown lands in the middle of the stored one.
    double storedX = 0;
    double storedY = 0;
    d.imageToDisplay.inverse().apply(x + 0.5, y + 0.5, storedX, storedY);
    const std::uint64_t request = ++d.probeRequest;
    d.queue.submit(d.group, kProbePriority,
                   [this, source = d.source, page = d.page, x, y, sx = static_cast<int>(std::floor(storedX)),
                    sy = static_cast<int>(std::floor(storedY)), request](std::stop_token stop) {
        const auto result = probePixel(*source, page, sx, sy, stop);
        if (!result) {
            return;
        }
        QVariantMap pixel;
        const auto colour = result->srgb.value_or(result->eightBit);
        pixel.insert(QStringLiteral("x"), x);
        pixel.insert(QStringLiteral("y"), y);
        pixel.insert(QStringLiteral("hex"), QString::fromStdString(hexText(result->eightBit)));
        pixel.insert(QStringLiteral("values"), QString::fromStdString(valuesText(*result)));
        pixel.insert(QStringLiteral("fractions"), QString::fromStdString(fractionsText(*result)));
        pixel.insert(QStringLiteral("gray"), result->channels == 1);
        pixel.insert(QStringLiteral("depth"), result->format == PixelFormat::Bgrx8 || result->format == PixelFormat::Gray8 ? QStringLiteral("8-bit")
                                              : result->format == PixelFormat::Gray16 || result->format == PixelFormat::Rgba16
                                                  ? QStringLiteral("16-bit")
                                                  : QStringLiteral("32-bit float"));
        pixel.insert(QStringLiteral("srgbHex"), result->srgb ? QString::fromStdString(hexText(*result->srgb)) : QString());
        pixel.insert(QStringLiteral("srgbValues"), result->srgb ? QString::fromStdString(eightBitText(*result->srgb)) : QString());
        pixel.insert(QStringLiteral("color"), QColor(colour[0], colour[1], colour[2]));
        QMetaObject::invokeMethod(this, [this, request, pixel = std::move(pixel)] {
            if (request == m_impl->probeRequest) {  // only the latest, while the mouse drags
                emit this->probed(pixel);
            }
        });
    });
}

void ImageView::prepareStraighten() {
    Impl& d = *m_impl;
    d.straightenBase = {};
    const std::uint64_t request = ++d.straightenRequest;
    if (!d.source || d.status != Status::Ready) {
        return;
    }
    // The smallest level that still has the size wanted, or the full one.
    const auto levels = d.levels();
    int level = 0;
    for (int i = 0; i < static_cast<int>(levels.size()); ++i) {
        const Size size = levels[static_cast<std::size_t>(i)].size;
        if (std::max(size.width, size.height) >= kStraightenBaseEdge) {
            level = i;
        }
    }
    const double scale = static_cast<double>(levels[static_cast<std::size_t>(level)].size.width) / d.fullSize.width;
    d.queue.submit(d.group, kStraightenPriority,
                   [this, source = d.source, page = d.page, level, scale, orientation = d.subimage().orientation,
                    range = d.displayRange, transform = d.colorTransform, under = d.backdrop, request](std::stop_token stop) {
        auto pixels = readLevel(*source, page, level, stop);
        if (!pixels) {
            return;
        }
        ImageBuffer base;
        try {
            ImageBuffer shown = toDisplay(applyOrientation(*pixels, orientation), range, transform.get(), under);
            // Larger levels than wanted, as a small file may have only its full size.
            const int edge = std::max(shown.width(), shown.height());
            if (edge > kStraightenBaseEdge * 2) {
                const double reduced = static_cast<double>(kStraightenBaseEdge) / edge;
                shown = resize(shown, {std::max(1, static_cast<int>(shown.width() * reduced)),
                                       std::max(1, static_cast<int>(shown.height() * reduced))}, stop);
                if (shown.isNull()) {
                    return;
                }
            }
            base = shown;
        } catch (const std::bad_alloc&) {
            return;
        }
        QMetaObject::invokeMethod(this, [this, base = std::move(base), scale, request] {
            Impl& d = *m_impl;
            if (request != d.straightenRequest) {
                return;
            }
            d.straightenBase = base;
            // Its pixels per pixel of the image as shown, whichever way it stands.
            const Size shown = d.shownSize();
            d.straightenBaseScale = shown.width > 0 ? static_cast<double>(base.width()) / shown.width : scale;
            emit straightenReady();
        });
    });
}

namespace {

std::vector<GuideLine> guideLines(const QVariantList& lines) {
    std::vector<GuideLine> result;
    for (const QVariant& value : lines) {
        const QVariantMap line = value.toMap();
        result.push_back({{line.value(QStringLiteral("x1")).toDouble(), line.value(QStringLiteral("y1")).toDouble()},
                          {line.value(QStringLiteral("x2")).toDouble(), line.value(QStringLiteral("y2")).toDouble()},
                          line.value(QStringLiteral("drawn")).toBool()});
    }
    return result;
}

}  // namespace

QVariantList ImageView::detectStraightLines() const {
    const Impl& d = *m_impl;
    QVariantList result;
    if (d.straightenBase.isNull()) {
        return result;
    }
    const double scale = d.straightenBaseScale;
    for (const GuideLine& line : detectLines(d.straightenBase)) {
        const double degrees = std::atan2(line.b.y - line.a.y, line.b.x - line.a.x) * 180 / std::numbers::pi;
        const double fromHorizontal = std::abs(std::remainder(degrees, 180.0));
        if (std::min(fromHorizontal, 90 - fromHorizontal) > kMaxLineLean) {
            continue;
        }
        result.append(QVariantMap{{QStringLiteral("x1"), line.a.x / scale}, {QStringLiteral("y1"), line.a.y / scale},
                                  {QStringLiteral("x2"), line.b.x / scale}, {QStringLiteral("y2"), line.b.y / scale}});
    }
    return result;
}

QVariantMap ImageView::fitStraightLines(const QVariantList& lines, int mode, double focal35mm, bool robust) const {
    const Impl& d = *m_impl;
    const auto guides = guideLines(lines);
    const TiltMode tiltMode = mode <= 0 ? TiltMode::Rotation : mode == 1 ? TiltMode::RotationAndVerticals : TiltMode::Full;
    const TiltFit fit = fitTilt(guides, d.shownSize(), d.focalPixelsFor(focal35mm), tiltMode, robust);
    QVariantList used;
    for (const bool u : fit.used) {
        used.append(u);
    }
    return {{QStringLiteral("roll"), fit.tilt.roll}, {QStringLiteral("pitch"), fit.tilt.pitch},
            {QStringLiteral("yaw"), fit.tilt.yaw}, {QStringLiteral("used"), used}};
}

double ImageView::focalLength35mm() const {
    const Impl& d = *m_impl;
    if (!d.fileSource) {
        return 0;
    }
    return d.fileSource->subimages()[static_cast<std::size_t>(d.filePage)].exif.focalLength35mm.value_or(0);
}

QSize ImageView::straightenedSize(double roll, double pitch, double yaw, double focal35mm, bool keepRatio, bool crop) const {
    const Impl& d = *m_impl;
    const Size size = planTilt(d.shownSize(), d.focalPixelsFor(focal35mm), {roll, pitch, yaw}, keepRatio, crop).size;
    return {size.width, size.height};
}

void ImageView::straighten(double roll, double pitch, double yaw, double focal35mm, bool keepRatio, bool crop) {
    Impl& d = *m_impl;
    const Tilt tilt{roll, pitch, yaw};
    if (!editable() || d.status != Status::Ready || tilt.isZero()) {
        return;
    }
    // Without the crop, strong perspective can make the result much larger than the picture.
    const Size size = planTilt(d.shownSize(), d.focalPixelsFor(focal35mm), tilt, keepRatio, crop).size;
    const bool fits = d.edit ? d.edit->canResizeTo(size).has_value() : Document(d.fileSource, d.filePage).canResizeTo(size).has_value();
    if (!fits) {
        return;
    }
    if (!d.edit) {
        d.edit.emplace(d.fileSource, d.filePage);
    }
    d.edit->apply(TiltEdit{tilt, d.focalPixelsFor(focal35mm), keepRatio, crop});
    d.showEdit();
}

QImage ImageView::straightenPreview(const Tilt& tilt, double focal35mm, bool keepRatio, bool crop) const {
    const Impl& d = *m_impl;
    if (d.straightenBase.isNull()) {
        return {};
    }
    const ImageBuffer& base = d.straightenBase;
    const TiltPlan plan = planTilt(base.size(), d.focalPixelsFor(focal35mm) * d.straightenBaseScale, tilt, keepRatio, crop);
    const Homography toSource = plan.transform.inverse() * Homography{{1, 0, plan.kept.x, 0, 1, plan.kept.y, 0, 0, 1}};
    try {
        // The corners left empty show as transparency is shown, on mid gray for now.
        return toQImage(crop ? warp(base, toSource.m, plan.size) : flattened(warp(base, toSource.m, plan.size, {}, WarpEdge::Clear)));
    } catch (const std::bad_alloc&) {
        return {};
    }
}

std::array<QPointF, 4> ImageView::straightenKept(const Tilt& tilt, double focal35mm, bool keepRatio, bool crop) const {
    const Impl& d = *m_impl;
    const TiltPlan plan = planTilt(d.shownSize(), d.focalPixelsFor(focal35mm), tilt, keepRatio, crop);
    const Homography back = plan.transform.inverse();
    const RectD k = plan.kept;
    std::array<QPointF, 4> corners;
    const std::array<PointD, 4> kept{{{k.x, k.y}, {k.x + k.width, k.y}, {k.x + k.width, k.y + k.height}, {k.x, k.y + k.height}}};
    for (std::size_t i = 0; i < 4; ++i) {
        const PointD p = back.apply(kept[i]);
        corners[i] = {p.x, p.y};
    }
    return corners;
}

void ImageView::zoomIn() {
    auto& vp = m_impl->viewport;
    vp.zoomSteps(1, vp.viewWidth() / 2, vp.viewHeight() / 2);
    m_impl->viewChanged();
}

void ImageView::zoomOut() {
    auto& vp = m_impl->viewport;
    vp.zoomSteps(-1, vp.viewWidth() / 2, vp.viewHeight() / 2);
    m_impl->viewChanged();
}

void ImageView::fitToView() {
    m_impl->viewport.setFitMode(FitMode::Fit);
    m_impl->viewChanged();
}

void ImageView::reload() {
    if (!m_impl->path.isEmpty()) {
        m_impl->open(m_impl->path);
    }
}

void ImageView::actualSize() {
    auto& vp = m_impl->viewport;
    vp.setFitMode(FitMode::Manual);
    vp.zoomBy(1 / vp.zoom(), vp.viewWidth() / 2, vp.viewHeight() / 2);
    m_impl->viewChanged();
}

QSGNode* ImageView::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    auto* root = static_cast<ImageRootNode*>(oldNode);
    Impl& d = *m_impl;

    if (!d.source) {
        delete root;
        return nullptr;
    }
    if (!root) {
        root = new ImageRootNode;
        // A new node means no textures, for instance after the scene graph was invalidated.
        if (!d.uploaded.empty()) {
            d.uploaded.clear();
            QMetaObject::invokeMethod(this, [this] { m_impl->requestVisibleTiles(); }, Qt::QueuedConnection);
        }
    }
    if (root->document != d.document) {
        root->clearTextures();
        root->document = d.document;
    }

    ++root->frame;
    d.uploadPending(*root);

    const std::vector<TileKey> tiles = d.tilesToDraw(*root);
    for (const TileKey& key : tiles) {
        root->textures.at(key).lastDrawnFrame = root->frame;
    }
    const auto filtering = d.resample.active || d.viewport.zoom() >= kNearestFilterZoom ? QSGTexture::Nearest : QSGTexture::Linear;
    d.syncTileNodes(*root, tiles, filtering);
    d.evictTextures(*root);
    root->setMatrix(d.storedToItemMatrix());
    return root;
}

void ImageView::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    m_impl->updateViewSize();
}

void ImageView::itemChange(ItemChange change, const ItemChangeData& value) {
    QQuickItem::itemChange(change, value);
    if (change == ItemDevicePixelRatioHasChanged || change == ItemSceneChange) {
        m_impl->updateViewSize();
    }
}

void ImageView::wheelEvent(QWheelEvent* event) {
    if (m_impl->wheelNavigates && !event->modifiers().testFlag(Qt::ControlModifier)) {
        const int notches = wholeSteps(m_impl->wheelTurned, event->angleDelta().y(), kWheelNotch);
        if (notches != 0) {
            emit navigateRequested(-notches);  // turning it towards the user goes on to the next
        }
        event->accept();
        return;
    }
    const double dpr = m_impl->devicePixelRatio();
    const double x = event->position().x() * dpr;
    const double y = event->position().y() * dpr;
    auto& vp = m_impl->viewport;
    if (fromTouchpad(*event)) {
        if (vp.zoom() != m_impl->touchpadShown) {
            m_impl->touchpadFree = freeZoomFor(vp.zoom(), kTouchpadHold);
        }
        m_impl->touchpadFree *= std::exp2(event->angleDelta().y() / kTouchpadPerDoubling);
        const double target = detentZoom(m_impl->touchpadFree, kTouchpadHold);
        vp.zoomBy(target / vp.zoom(), x, y);
        if (vp.zoom() != target) {  // held at a limit: turning back moves at once
            m_impl->touchpadFree = freeZoomFor(vp.zoom(), kTouchpadHold);
        }
        m_impl->touchpadShown = vp.zoom();
        m_impl->viewChanged();
    } else if (const int steps = wholeSteps(m_impl->zoomTurned, event->angleDelta().y(), kWheelNotch); steps != 0) {
        vp.zoomSteps(steps, x, y);
        m_impl->viewChanged();
    }
    event->accept();
}

void ImageView::mousePressEvent(QMouseEvent* event) {
    m_impl->lastDragPosition = event->position();
    m_impl->pressed = true;
    if (m_impl->canUseLoupe()) {
        m_impl->startLoupe(event->position());
    }
    m_impl->viewChanged();
    event->accept();
}

void ImageView::mouseMoveEvent(QMouseEvent* event) {
    const QPointF delta = (event->position() - m_impl->lastDragPosition) * m_impl->devicePixelRatio();
    m_impl->lastDragPosition = event->position();
    m_impl->viewport.panBy(delta.x(), delta.y());
    m_impl->viewChanged();
    event->accept();
}

void ImageView::mouseReleaseEvent(QMouseEvent* event) {
    m_impl->endPress();
    event->accept();
}

void ImageView::mouseUngrabEvent() {
    if (m_impl->pressed) {
        m_impl->endPress();
    }
}

void ImageView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (m_impl->viewport.fitMode() == FitMode::Manual) {
        m_impl->viewport.setFitMode(FitMode::ShrinkToFit);
        m_impl->viewChanged();
    } else {
        actualSize();
    }
    event->accept();
}

}  // namespace iv
