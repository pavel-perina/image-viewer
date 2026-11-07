#include "ui/thumbnail_store.h"

#include <QMetaObject>

#include <stop_token>

#include "core/thumbnail_cache.h"
#include "core/work_queue.h"
#include "ui/thumbnail_provider.h"

namespace iv {

namespace {

constexpr int kStorePriority = 100;

}  // namespace

struct ThumbnailStore::Impl {
    WorkQueue& queue = WorkQueue::global();
    WorkQueue::GroupId group = queue.createGroup();
};

ThumbnailStore::ThumbnailStore(QObject* parent) : QObject(parent), m_impl(std::make_unique<Impl>()) {}

ThumbnailStore::~ThumbnailStore() {
    // A running task posts its result to this object, so it must finish first.
    m_impl->queue.waitGroup(m_impl->group);
}

void ThumbnailStore::measure() {
    m_impl->queue.submit(m_impl->group, kStorePriority, [this](std::stop_token) {
        ThumbnailCache* cache = ThumbnailProvider::diskCache();
        const ThumbnailCache::Usage usage = cache ? cache->usage() : ThumbnailCache::Usage{};
        QMetaObject::invokeMethod(this, [this, available = cache != nullptr, usage] {
            emit measured(available, static_cast<double>(usage.count), static_cast<double>(usage.bytes));
        });
    });
}

void ThumbnailStore::clear() {
    m_impl->queue.submit(m_impl->group, kStorePriority, [this](std::stop_token) {
        ThumbnailCache* cache = ThumbnailProvider::diskCache();
        const bool done = cache && cache->clear();
        QMetaObject::invokeMethod(this, [this, done] { emit cleared(done); });
    });
}

}  // namespace iv
