#include "ui/thumbnail_store.h"

#include <QMetaObject>
#include <QPointer>
#include <QList>
#include <QLocale>

#include <stop_token>
#include <utility>

#include "core/thumbnail_cache.h"
#include "core/work_queue.h"
#include "ui/thumbnail_provider.h"
#include "ui/preferences.h"

namespace iv {

namespace {

constexpr int kStorePriority = 100;

}  // namespace

struct ThumbnailStore::Impl {
    WorkQueue& queue = WorkQueue::global();
    WorkQueue::GroupId group = queue.createGroup();
    bool measuring = false;
    bool clearing = false;
    bool available = false;
    bool measuredOnce = false;
    double count = 0;
    double bytes = 0;
    QList<QPointer<QObject>> clearOwners;
};

ThumbnailStore::ThumbnailStore(QObject* parent) : QObject(parent), m_impl(std::make_unique<Impl>()) {
    connect(Preferences::instance(), &Preferences::thumbnailCacheMiBChanged, this, &ThumbnailStore::measure);
}

ThumbnailStore::~ThumbnailStore() {
    // A running task posts its result to this object, so it must finish first.
    m_impl->queue.waitGroup(m_impl->group);
}

void ThumbnailStore::measure() {
    if (m_impl->measuring) return;
    m_impl->measuring = true;
    emit busyChanged();
    m_impl->queue.submit(m_impl->group, kStorePriority, [this](std::stop_token) {
        ThumbnailCache* cache = ThumbnailProvider::diskCache();
        const ThumbnailCache::Usage usage = cache ? cache->usage() : ThumbnailCache::Usage{};
        QMetaObject::invokeMethod(this, [this, available = cache != nullptr, usage] {
            m_impl->measuring = false;
            m_impl->measuredOnce = true;
            m_impl->available = available;
            m_impl->count = static_cast<double>(usage.count);
            m_impl->bytes = static_cast<double>(usage.bytes);
            emit usageChanged();
            emit busyChanged();
            emit measured(available, static_cast<double>(usage.count), static_cast<double>(usage.bytes));
            const auto owners = std::exchange(m_impl->clearOwners, {});
            for (const auto& owner : owners) {
                if (owner) emit clearRequested(owner, available, m_impl->count, m_impl->bytes);
            }
        });
    });
}

void ThumbnailStore::requestClear(QObject* owner) {
    if (!owner || m_impl->clearing) return;
    if (!m_impl->clearOwners.contains(owner)) m_impl->clearOwners.append(owner);
    measure();
}

bool ThumbnailStore::clear() {
    if (m_impl->clearing) return false;
    m_impl->clearing = true;
    emit busyChanged();
    m_impl->queue.submit(m_impl->group, kStorePriority, [this](std::stop_token) {
        ThumbnailCache* cache = ThumbnailProvider::diskCache();
        const bool done = cache && cache->clear();
        QMetaObject::invokeMethod(this, [this, done] {
            m_impl->clearing = false;
            emit busyChanged();
            emit cleared(done);
            measure();
        });
    });
    return true;
}

bool ThumbnailStore::available() const { return m_impl->available; }
bool ThumbnailStore::measuredOnce() const { return m_impl->measuredOnce; }
double ThumbnailStore::count() const { return m_impl->count; }
double ThumbnailStore::bytes() const { return m_impl->bytes; }
bool ThumbnailStore::busy() const { return m_impl->measuring || m_impl->clearing; }
QString ThumbnailStore::usageText() const {
    return QStringLiteral("%1 · %2 stored thumbnails")
        .arg(QLocale().formattedDataSize(static_cast<qint64>(m_impl->bytes), 1, QLocale::DataSizeIecFormat),
             QLocale().toString(static_cast<qulonglong>(m_impl->count)));
}

}  // namespace iv
