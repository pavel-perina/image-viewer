#include "ui/subfolder_list.h"

#include <QCollator>
#include <QDir>

#include <algorithm>
#include <cstdint>
#include <stop_token>

#include "core/work_queue.h"

namespace iv {

namespace {

constexpr int kListPriority = 100;

}  // namespace

struct SubfolderList::Impl {
    WorkQueue& queue = WorkQueue::global();
    WorkQueue::GroupId group = queue.createGroup();
    std::uint64_t generation = 0;
    QString path;
    QStringList folders;
    bool loading = false;
};

SubfolderList::SubfolderList(QObject* parent) : QObject(parent), m_impl(std::make_unique<Impl>()) {}

SubfolderList::~SubfolderList() {
    // A running task posts its result to this object, so it must finish first.
    m_impl->queue.cancelGroup(m_impl->group);
    m_impl->queue.waitGroup(m_impl->group);
}

QString SubfolderList::path() const { return m_impl->path; }
QStringList SubfolderList::folders() const { return m_impl->folders; }
bool SubfolderList::loading() const { return m_impl->loading; }

void SubfolderList::setPath(const QString& path) {
    m_impl->queue.cancelGroup(m_impl->group);
    const std::uint64_t generation = ++m_impl->generation;
    m_impl->path = path;
    m_impl->folders.clear();
    m_impl->loading = !path.isEmpty();
    emit pathChanged();
    emit foldersChanged();
    if (path.isEmpty()) {
        return;
    }
    m_impl->queue.submit(m_impl->group, kListPriority, [this, path, generation](std::stop_token) {
        QStringList names = QDir(path).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::ranges::sort(names, collator);
        QMetaObject::invokeMethod(this, [this, generation, names = std::move(names)] {
            if (generation == m_impl->generation) {
                m_impl->folders = names;
                m_impl->loading = false;
                emit foldersChanged();
            }
        });
    });
}

}  // namespace iv
