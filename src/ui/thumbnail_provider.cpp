#include "ui/thumbnail_provider.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QQuickTextureFactory>
#include <QStandardPaths>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include "core/image_buffer.h"
#include "core/thumbnail_cache.h"
#include "core/work_queue.h"
#include "ui/color_settings.h"
#include "ui/qimage_bridge.h"

namespace iv {

namespace {

constexpr int kDefaultEdge = 256;
// Below every other kind of work, so opening an image is never held up by thumbnails.
constexpr int kBasePriority = -1'000'000'000;

// Enough for about 50 000 thumbnails at the size the browser shows them.
constexpr std::uint64_t kCacheBytes = 512ull * 1024 * 1024;

// Opened on first use by a worker, as it can touch the disk. Null if it cannot be used.
ThumbnailCache* diskCache() {
    static const std::unique_ptr<ThumbnailCache> cache = []() -> std::unique_ptr<ThumbnailCache> {
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (folder.isEmpty() || !QDir().mkpath(folder)) {
            return nullptr;
        }
        return ThumbnailCache::open(QFileInfo(folder + QStringLiteral("/thumbnails.db")).filesystemFilePath(), kCacheBytes);
    }();
    return cache.get();
}

class ThumbnailResponse final : public QQuickImageResponse {
public:
    ThumbnailResponse(const QString& path, int priority, int edge) : m_link(std::make_shared<Link>()) {
        m_link->response = this;
        m_task = WorkQueue::global().submit(
            group(), priority,
            [link = m_link, edge, file = QFileInfo(path).filesystemFilePath()](std::stop_token stop) {
                auto thumbnail = makeCachedThumbnail(diskCache(), file, edge, stop, ColorSettings::target());
                const std::scoped_lock lock(link->mutex);
                if (link->response) {
                    link->response->complete(thumbnail);
                }
            });
    }

    ~ThumbnailResponse() override {
        // Afterwards the worker cannot reach this object any more.
        const std::scoped_lock lock(m_link->mutex);
        m_link->response = nullptr;
        WorkQueue::global().cancel(m_task);
    }

    QQuickTextureFactory* textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override { return m_error; }

    void cancel() override {
        WorkQueue::global().cancel(m_task);
        // Qt deletes a response only after finished(), and a task that was still queued will
        // never run to emit it.
        const std::scoped_lock lock(m_link->mutex);
        finish(QImage(), QStringLiteral("cancelled"));
    }

private:
    struct Link {
        std::mutex mutex;
        ThumbnailResponse* response = nullptr;
    };

    static WorkQueue::GroupId group() {
        static const WorkQueue::GroupId id = WorkQueue::global().createGroup();
        return id;
    }

    void complete(const Result<ImageBuffer>& thumbnail) {
        if (thumbnail) {
            finish(toQImage(*thumbnail), QString());
        } else {
            finish(QImage(), QString::fromStdString(thumbnail.error()));
        }
    }

    // Call with the link's mutex held. Only the first result counts.
    void finish(const QImage& image, const QString& error) {
        if (m_finished) {
            return;
        }
        m_finished = true;
        m_image = image;
        m_error = error;
        emit finished();  // documented as safe to emit from any thread
    }

    std::shared_ptr<Link> m_link;
    WorkQueue::TaskId m_task = 0;
    bool m_finished = false;
    QImage m_image;
    QString m_error;
};

}  // namespace

QString ThumbnailProvider::urlForPath(const QString& path, qint64 modified) {
    // Hex keeps any file name intact on its way through the URL handling.
    return QStringLiteral("image://") + QLatin1StringView(kProviderId) + QLatin1Char('/') +
           QString::fromLatin1(path.toUtf8().toHex()) + QLatin1Char('/') + QString::number(ColorSettings::generation()) + QLatin1Char('/') + QString::number(modified);
}

QQuickImageResponse* ThumbnailProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
    // The latest request is the one the user is looking at now.
    static std::atomic<int> sequence = 0;
    const QString path = QString::fromUtf8(QByteArray::fromHex(id.section(QLatin1Char('/'), 0, 0).toLatin1()));
    const int requested = std::max(requestedSize.width(), requestedSize.height());
    const int edge = requested > 0 ? requested : kDefaultEdge;
    return new ThumbnailResponse(path, kBasePriority + sequence.fetch_add(1, std::memory_order_relaxed), edge);
}

}  // namespace iv
