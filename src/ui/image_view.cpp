#include "ui/image_view.h"

#include <QFileInfo>
#include <QImage>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QSGTransformNode>
#include <QWheelEvent>
#include <QtDebug>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <utility>
#include <vector>

#include "core/image_source.h"
#include "core/viewport.h"
#include "core/work_queue.h"

namespace iv {

namespace {

constexpr int kOpenPriority = 1000;
constexpr int kTilePriority = 500;
constexpr double kZoomStep = 1.25;
// Tile textures kept on the GPU. Past this, the least recently drawn ones are dropped.
constexpr qint64 kTextureBudgetBytes = 768ll * 1024 * 1024;
// Limits how much is uploaded per frame, so a burst of tiles cannot stall panning.
constexpr qint64 kUploadBytesPerFrame = 64ll * 1024 * 1024;
// From this zoom up, show individual pixels instead of blurring them.
constexpr double kNearestFilterZoom = 2.0;

struct TileKey {
    int level = 0;
    int col = 0;
    int row = 0;
    auto operator<=>(const TileKey&) const = default;
};

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

// Wraps a tile without copying; the QImage keeps the pixels alive until it is uploaded.
QImage toQImage(const ImageBuffer& buffer) {
    auto* keepAlive = new ImageBuffer(buffer);
    return QImage(
        reinterpret_cast<const uchar*>(buffer.data()), buffer.width(), buffer.height(),
        static_cast<qsizetype>(buffer.stride()), QImage::Format_RGB32,
        [](void* info) { delete static_cast<ImageBuffer*>(info); }, keepAlive);
}

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

    // The displayed document. It stays until a newly requested one is ready to show.
    std::uint64_t document = 0;
    std::shared_ptr<ImageSource> source;
    Size fullSize;
    Affine imageToDisplay;
    Viewport viewport;
    QPointF lastDragPosition;

    std::map<TileKey, WorkQueue::TaskId> inFlight;
    std::map<TileKey, QImage> pendingUpload;
    std::set<TileKey> uploaded;
    std::set<TileKey> failed;

    double devicePixelRatio() const {
        return q->window() ? q->window()->effectiveDevicePixelRatio() : 1.0;
    }

    std::span<const LevelInfo> levels() const { return source->subimages()[0].levels; }

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

    void viewChanged() {
        emit q->zoomChanged();
        if (source) {
            requestVisibleTiles();
        }
        q->update();
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
        return imageToDisplay.inverse().mapRect(viewport.visibleRect());
    }

    void requestVisibleTiles() {
        const int level = chooseLevel(levels(), viewport.zoom());
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
        for (const TileKey& key : wanted) {
            if (!uploaded.contains(key) && !pendingUpload.contains(key) && !inFlight.contains(key) &&
                !failed.contains(key)) {
                requestTile(key);
            }
        }
    }

    void requestTile(const TileKey& key) {
        const std::uint64_t doc = document;
        const auto id = queue.submit(group, kTilePriority,
                                     [this, source = source, key, doc](std::stop_token stop) {
            auto tile = source->readTile({0, key.level, key.col, key.row}, stop);
            QMetaObject::invokeMethod(q, [this, key, doc, tile = std::move(tile)] {
                onTileRead(key, doc, tile);
            });
        });
        inFlight.emplace(key, id);
    }

    void onTileRead(const TileKey& key, std::uint64_t doc, const Result<ImageBuffer>& tile) {
        if (doc != document) {
            return;
        }
        inFlight.erase(key);
        if (tile) {
            pendingUpload.insert_or_assign(key, toQImage(*tile));
            q->update();
        } else if (tile.error() != "cancelled") {
            qWarning() << "Cannot read tile of" << path << ":" << QString::fromStdString(tile.error());
            failed.insert(key);
        }
    }

    // What the worker hands over: an opened file, with its smallest level already decoded.
    struct Opened {
        std::shared_ptr<ImageSource> source;
        std::vector<std::pair<TileKey, ImageBuffer>> preview;
    };

    static Result<Opened> openWithPreview(const std::filesystem::path& file, const std::stop_token& stop) {
        auto source = openImageSource(file);
        if (!source) {
            return std::unexpected(source.error());
        }
        const SubimageInfo& subimage = (*source)->subimages()[0];
        if (subimage.format != PixelFormat::Bgrx8) {
            return std::unexpected("this pixel format cannot be displayed yet");
        }

        Opened opened{*source, {}};
        const int coarsest = static_cast<int>(subimage.levels.size()) - 1;
        const LevelInfo& level = subimage.levels.back();
        const int cols = (level.size.width + level.tileSize.width - 1) / level.tileSize.width;
        const int rows = (level.size.height + level.tileSize.height - 1) / level.tileSize.height;
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                auto tile = (*source)->readTile({0, coarsest, col, row}, stop);
                if (!tile) {
                    return std::unexpected(tile.error());
                }
                opened.preview.emplace_back(TileKey{coarsest, col, row}, *tile);
            }
        }
        return opened;
    }

    void open(const QString& newPath) {
        const std::uint64_t request = ++latestRequest;
        queue.cancelGroup(group);
        inFlight.clear();

        if (newPath.isEmpty()) {
            closeDocument();
            setStatus(Status::Null);
            return;
        }
        setStatus(Status::Loading);

        queue.submit(group, kOpenPriority,
                     [this, file = QFileInfo(newPath).filesystemFilePath(), request](std::stop_token stop) {
            auto opened = openWithPreview(file, stop);
            QMetaObject::invokeMethod(q, [this, request, opened = std::move(opened)] {
                onOpened(request, opened);
            });
        });
    }

    void onOpened(std::uint64_t request, const Result<Opened>& opened) {
        if (request != latestRequest) {
            return;
        }
        if (!opened) {
            closeDocument();
            setStatus(Status::Error, QString::fromStdString(opened.error()));
            return;
        }

        ++document;
        source = opened->source;
        const SubimageInfo& subimage = source->subimages()[0];
        fullSize = subimage.levels.front().size;
        imageToDisplay = displayFromStored(fullSize, subimage.orientation);

        inFlight.clear();
        uploaded.clear();
        failed.clear();
        pendingUpload.clear();
        for (const auto& [key, tile] : opened->preview) {
            pendingUpload.emplace(key, toQImage(tile));
        }

        if (viewport.fitMode() == FitMode::Manual) {
            viewport.setFitMode(FitMode::ShrinkToFit);
        }
        viewport.setImageSize(displaySize(fullSize, subimage.orientation));
        setStatus(Status::Ready);
        viewChanged();
    }

    void closeDocument() {
        ++document;
        source.reset();
        inFlight.clear();
        uploaded.clear();
        failed.clear();
        pendingUpload.clear();
        viewport.setImageSize({});
        emit q->zoomChanged();
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
        const int target = chooseLevel(levels(), viewport.zoom());
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
            node->setFiltering(filtering);
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
    return m_impl->source ? displaySize(m_impl->fullSize, m_impl->source->subimages()[0].orientation).width : 0;
}

int ImageView::imageHeight() const {
    return m_impl->source ? displaySize(m_impl->fullSize, m_impl->source->subimages()[0].orientation).height : 0;
}

double ImageView::zoom() const { return m_impl->viewport.zoom(); }

void ImageView::zoomIn() {
    auto& vp = m_impl->viewport;
    vp.zoomBy(kZoomStep, vp.viewWidth() / 2, vp.viewHeight() / 2);
    m_impl->viewChanged();
}

void ImageView::zoomOut() {
    auto& vp = m_impl->viewport;
    vp.zoomBy(1 / kZoomStep, vp.viewWidth() / 2, vp.viewHeight() / 2);
    m_impl->viewChanged();
}

void ImageView::fitToView() {
    m_impl->viewport.setFitMode(FitMode::Fit);
    m_impl->viewChanged();
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
    const auto filtering = d.viewport.zoom() >= kNearestFilterZoom ? QSGTexture::Nearest : QSGTexture::Linear;
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
    const double dpr = m_impl->devicePixelRatio();
    const double notches = event->angleDelta().y() / 120.0;
    m_impl->viewport.zoomBy(std::pow(kZoomStep, notches), event->position().x() * dpr, event->position().y() * dpr);
    m_impl->viewChanged();
    event->accept();
}

void ImageView::mousePressEvent(QMouseEvent* event) {
    m_impl->lastDragPosition = event->position();
    event->accept();
}

void ImageView::mouseMoveEvent(QMouseEvent* event) {
    const QPointF delta = (event->position() - m_impl->lastDragPosition) * m_impl->devicePixelRatio();
    m_impl->lastDragPosition = event->position();
    m_impl->viewport.panBy(delta.x(), delta.y());
    m_impl->viewChanged();
    event->accept();
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
