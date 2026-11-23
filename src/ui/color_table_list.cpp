#include "ui/color_table_list.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "core/work_queue.h"

namespace iv {

namespace {

constexpr int kListPriority = 100;
constexpr int kRefreshDelayMs = 300;

struct Table {
    QString name;
    QString path;
};

std::vector<Table> findTables(const QString& folder, std::stop_token stop) {
    std::vector<Table> tables;
    const QDir root(folder);
    QDirIterator it(folder, {QStringLiteral("*.cube")}, QDir::Files | QDir::Readable,
                    QDirIterator::Subdirectories);
    while (it.hasNext() && !stop.stop_requested() && std::cmp_less(tables.size(), ColorTableList::kMostTables)) {
        const QString path = it.next();
        const QString relative = root.relativeFilePath(path);
        tables.push_back({relative.left(relative.size() - 5), path});
    }
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::ranges::sort(tables, [&](const Table& a, const Table& b) { return collator.compare(a.name, b.name) < 0; });
    return tables;
}

}  // namespace

struct ColorTableList::Impl {
    QString folder;
    std::vector<Table> tables;
    QStringList names;
    bool loading = false;
    std::uint64_t generation = 0;
    QFileSystemWatcher watcher;
    QTimer refreshTimer;
    WorkQueue& queue = WorkQueue::global();
    WorkQueue::GroupId group = queue.createGroup();
};

ColorTableList::ColorTableList(QObject* parent) : QObject(parent), m_impl(std::make_unique<Impl>()) {
    m_impl->refreshTimer.setSingleShot(true);
    m_impl->refreshTimer.setInterval(kRefreshDelayMs);
    connect(&m_impl->refreshTimer, &QTimer::timeout, this, &ColorTableList::refresh);
    connect(&m_impl->watcher, &QFileSystemWatcher::directoryChanged, &m_impl->refreshTimer, qOverload<>(&QTimer::start));
}

ColorTableList::~ColorTableList() {
    // A running task posts its result to this object, so it must finish first.
    m_impl->queue.cancelGroup(m_impl->group);
    m_impl->queue.waitGroup(m_impl->group);
}

QString ColorTableList::folder() const { return m_impl->folder; }

void ColorTableList::setFolder(const QString& folder) {
    if (folder == m_impl->folder) {
        return;
    }
    if (!m_impl->folder.isEmpty()) {
        m_impl->watcher.removePath(m_impl->folder);
    }
    m_impl->folder = folder;
    if (!folder.isEmpty() && QFileInfo(folder).isDir()) {
        m_impl->watcher.addPath(folder);
    }
    emit folderChanged();
    refresh();
}

QStringList ColorTableList::names() const { return m_impl->names; }

bool ColorTableList::loading() const { return m_impl->loading; }

QString ColorTableList::pathOf(const QString& name) const {
    const auto found = std::ranges::find(m_impl->tables, name, &Table::name);
    return found != m_impl->tables.end() ? found->path : QString();
}

QUrl ColorTableList::urlOf(const QString& name) const {
    const QString path = pathOf(name);
    return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path);
}

QString ColorTableList::step(const QString& name, int by) const {
    // Positions count no table as 0 and the tables from 1.
    const qsizetype at = m_impl->names.indexOf(name) + 1;
    const qsizetype next = std::clamp<qsizetype>(at + by, 0, m_impl->names.size());
    return next == 0 ? QString() : m_impl->names.at(next - 1);
}

QString ColorTableList::nameOf(const QString& path) const {
    const QString wanted = QFileInfo(path).absoluteFilePath();
    const auto found = std::ranges::find_if(m_impl->tables, [&](const Table& table) { return QFileInfo(table.path).absoluteFilePath() == wanted; });
    return found != m_impl->tables.end() ? found->name : QString();
}

void ColorTableList::refresh() {
    m_impl->queue.cancelGroup(m_impl->group);
    const std::uint64_t generation = ++m_impl->generation;
    const QString folder = m_impl->folder;
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
        if (!m_impl->tables.empty()) {
            m_impl->tables.clear();
            m_impl->names.clear();
            emit namesChanged();
        }
        if (m_impl->loading) {
            m_impl->loading = false;
            emit loadingChanged();
        }
        return;
    }
    if (!m_impl->loading) {
        m_impl->loading = true;
        emit loadingChanged();
    }
    m_impl->queue.submit(m_impl->group, kListPriority, [this, folder, generation](std::stop_token stop) {
        std::vector<Table> tables = findTables(folder, stop);
        if (stop.stop_requested()) {
            return;
        }
        QMetaObject::invokeMethod(this, [this, generation, tables = std::move(tables)]() mutable {
            if (generation != m_impl->generation) {
                return;
            }
            QStringList names;
            names.reserve(static_cast<qsizetype>(tables.size()));
            for (const Table& table : tables) {
                names << table.name;
            }
            m_impl->tables = std::move(tables);
            if (names != m_impl->names) {
                m_impl->names = std::move(names);
                emit namesChanged();
            }
            m_impl->loading = false;
            emit loadingChanged();
        });
    });
}

}  // namespace iv
