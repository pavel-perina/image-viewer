#include "ui/thumbnail_provider.h"

#include <QByteArray>
#include <QFileInfo>
#include <QImage>
#include <QQuickTextureFactory>

#include <atomic>
#include <memory>
#include <mutex>

#include "core/image_buffer.h"
#include "core/thumbnail.h"
#include "core/work_queue.h"

namespace iv {

namespace {

constexpr int kThumbnailEdge = 256;
// Below every other kind of work, so opening an image is never held up by thumbnails.
constexpr int kBasePriority = -1'000'000'000;

QImage toQImage(const ImageBuffer& buffer) {
    auto* keepAlive = new ImageBuffer(buffer);
    return QImage(
        reinterpret_cast<const uchar*>(buffer.data()), buffer.width(), buffer.height(),
        static_cast<qsizetype>(buffer.stride()), QImage::Format_RGB32,
        [](void* info) { delete static_cast<ImageBuffer*>(info); }, keepAlive);
}

class ThumbnailResponse final : public QQuickImageResponse {
public:
    ThumbnailResponse(const QString& path, int priority) : m_link(std::make_shared<Link>()) {
        m_link->response = this;
        m_task = WorkQueue::global().submit(
            group(), priority,
            [link = m_link, file = QFileInfo(path).filesystemFilePath()](std::stop_token stop) {
                auto thumbnail = makeThumbnail(file, kThumbnailEdge, stop);
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

QString ThumbnailProvider::urlForPath(const QString& path) {
    // Hex keeps any file name intact on its way through the URL handling.
    return QStringLiteral("image://") + QLatin1StringView(kProviderId) + QLatin1Char('/') +
           QString::fromLatin1(path.toUtf8().toHex());
}

QQuickImageResponse* ThumbnailProvider::requestImageResponse(const QString& id, const QSize&) {
    // The latest request is the one the user is looking at now.
    static std::atomic<int> sequence = 0;
    const QString path = QString::fromUtf8(QByteArray::fromHex(id.toLatin1()));
    return new ThumbnailResponse(path, kBasePriority + sequence.fetch_add(1, std::memory_order_relaxed));
}

}  // namespace iv
