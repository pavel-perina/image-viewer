#include "ui/folder_listing.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLocale>
#include <QMimeDatabase>
#include <QTimer>

#include <algorithm>
#include <cstdint>
#include <stop_token>
#include <vector>

#include "core/image_details.h"
#include "core/work_queue.h"

namespace iv {

namespace {

constexpr int kListPriority = 100;
constexpr int kRefreshDelayMs = 300;

struct Entry {
    QString name;
    QString path;
    bool folder = false;
    qint64 size = -1;
    QDateTime modified;
    QString type;
};

std::vector<Entry> readFolder(const QString& path, const QStringList& extensions, bool showFiles) {
    QDir::Filters filters = QDir::Dirs | QDir::NoDotAndDotDot;
    if (showFiles) {
        filters |= QDir::Files;
    }
    const QFileInfoList infos = QDir(path).entryInfoList(filters, QDir::NoSort);
    QMimeDatabase mimes;
    QHash<QString, QString> typeBySuffix;
    std::vector<Entry> entries;
    entries.reserve(static_cast<std::size_t>(infos.size()));
    for (const QFileInfo& info : infos) {
        if (info.isHidden()) {
            continue;
        }
        Entry entry{info.fileName(), info.absoluteFilePath(), info.isDir(), -1, info.lastModified(), {}};
        if (entry.folder) {
            entry.type = QStringLiteral("Folder");
        } else {
            const QString suffix = info.suffix().toLower();
            if (!extensions.isEmpty() && !extensions.contains(suffix)) {
                continue;
            }
            entry.size = info.size();
            auto known = typeBySuffix.find(suffix);
            if (known == typeBySuffix.end()) {
                const QMimeType mime = mimes.mimeTypeForFile(info, QMimeDatabase::MatchExtension);
                const QString type = suffix.isEmpty() ? QStringLiteral("File")
                                     : mime.isDefault() ? suffix.toUpper() + QStringLiteral(" file")
                                                        : mime.comment();
                known = typeBySuffix.insert(suffix, type);
            }
            entry.type = *known;
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

}  // namespace

struct FolderListing::Impl {
    WorkQueue& queue = WorkQueue::global();
    WorkQueue::GroupId group = queue.createGroup();
    std::uint64_t generation = 0;
    QString path;
    QStringList extensions;
    bool showFiles = true;
    SortKey sortKey = SortKey::Name;
    bool descending = false;
    bool loading = false;
    std::vector<Entry> entries;
    QFileSystemWatcher watcher;
    QTimer refreshTimer;

    // Folders always first, then by the key, ties by name.
    void sort() {
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        const auto byName = [&](const Entry& a, const Entry& b) { return collator.compare(a.name, b.name) < 0; };
        std::ranges::stable_sort(entries, [&](const Entry& a, const Entry& b) {
            if (a.folder != b.folder) {
                return a.folder;
            }
            const Entry& x = descending ? b : a;
            const Entry& y = descending ? a : b;
            switch (sortKey) {
            case SortKey::Size:
                if (x.size != y.size) {
                    return x.size < y.size;
                }
                break;
            case SortKey::Type:
                if (const int c = collator.compare(x.type, y.type); c != 0) {
                    return c < 0;
                }
                break;
            case SortKey::Modified:
                if (x.modified != y.modified) {
                    return x.modified < y.modified;
                }
                break;
            case SortKey::Name:
                break;
            }
            return byName(x, y);
        });
    }
};

FolderListing::FolderListing(QObject* parent) : QAbstractListModel(parent), m_impl(std::make_unique<Impl>()) {
    m_impl->refreshTimer.setSingleShot(true);
    m_impl->refreshTimer.setInterval(kRefreshDelayMs);
    connect(&m_impl->refreshTimer, &QTimer::timeout, this, &FolderListing::refresh);
    connect(&m_impl->watcher, &QFileSystemWatcher::directoryChanged, &m_impl->refreshTimer, qOverload<>(&QTimer::start));
}

FolderListing::~FolderListing() {
    // A running task posts its result to this object, so it must finish first.
    m_impl->queue.cancelGroup(m_impl->group);
    m_impl->queue.waitGroup(m_impl->group);
}

QString FolderListing::path() const { return m_impl->path; }

void FolderListing::setPath(const QString& path) {
    if (path == m_impl->path) {
        return;
    }
    if (!m_impl->path.isEmpty()) {
        m_impl->watcher.removePath(m_impl->path);
    }
    m_impl->path = path;
    if (!path.isEmpty()) {
        m_impl->watcher.addPath(path);
    }
    emit pathChanged();
    beginResetModel();
    m_impl->entries.clear();
    endResetModel();
    emit countChanged();
    refresh();
}

QStringList FolderListing::extensions() const { return m_impl->extensions; }

void FolderListing::setExtensions(const QStringList& extensions) {
    QStringList lower;
    for (const QString& extension : extensions) {
        lower << extension.toLower();
    }
    if (lower != m_impl->extensions) {
        m_impl->extensions = lower;
        emit extensionsChanged();
        refresh();
    }
}

bool FolderListing::showFiles() const { return m_impl->showFiles; }

void FolderListing::setShowFiles(bool show) {
    if (show != m_impl->showFiles) {
        m_impl->showFiles = show;
        emit showFilesChanged();
        refresh();
    }
}

FolderListing::SortKey FolderListing::sortKey() const { return m_impl->sortKey; }

void FolderListing::setSortKey(SortKey key) {
    if (key != m_impl->sortKey) {
        m_impl->sortKey = key;
        beginResetModel();
        m_impl->sort();
        endResetModel();
        emit sortChanged();
    }
}

bool FolderListing::sortDescending() const { return m_impl->descending; }

void FolderListing::setSortDescending(bool descending) {
    if (descending != m_impl->descending) {
        m_impl->descending = descending;
        beginResetModel();
        m_impl->sort();
        endResetModel();
        emit sortChanged();
    }
}

bool FolderListing::loading() const { return m_impl->loading; }
int FolderListing::count() const { return static_cast<int>(m_impl->entries.size()); }

int FolderListing::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }

QVariant FolderListing::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
        return {};
    }
    const Entry& entry = m_impl->entries[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return entry.name;
    case PathRole: return entry.path;
    case IsFolderRole: return entry.folder;
    case SizeRole: return entry.size;
    case SizeTextRole:
        return entry.folder ? QString() : QString::fromStdString(shortDataSize(static_cast<std::uint64_t>(entry.size)));
    case TypeRole: return entry.type;
    case ModifiedRole: return entry.modified;
    case ModifiedTextRole: return QLocale().toString(entry.modified, QLocale::ShortFormat);
    default: return {};
    }
}

QHash<int, QByteArray> FolderListing::roleNames() const {
    return {{NameRole, "name"},       {PathRole, "path"},         {IsFolderRole, "isFolder"},
            {SizeRole, "size"},       {SizeTextRole, "sizeText"}, {TypeRole, "type"},
            {ModifiedRole, "modified"}, {ModifiedTextRole, "modifiedText"}};
}

int FolderListing::indexOf(const QString& name) const {
    const auto found = std::ranges::find(m_impl->entries, name, &Entry::name);
    return found == m_impl->entries.end() ? -1 : static_cast<int>(found - m_impl->entries.begin());
}

QVariantMap FolderListing::get(int row) const {
    QVariantMap map;
    if (row < 0 || row >= count()) {
        return map;
    }
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.begin(); it != roles.end(); ++it) {
        map.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    }
    return map;
}

QString FolderListing::kindOf(const QString& path) const {
    const QFileInfo info(path);
    return !info.exists() ? QString() : info.isDir() ? QStringLiteral("folder") : QStringLiteral("file");
}

void FolderListing::refresh() {
    m_impl->queue.cancelGroup(m_impl->group);
    const std::uint64_t generation = ++m_impl->generation;
    const QString path = m_impl->path;
    if (path.isEmpty()) {
        return;
    }
    if (!m_impl->loading) {
        m_impl->loading = true;
        emit loadingChanged();
    }
    m_impl->queue.submit(m_impl->group, kListPriority,
                         [this, path, generation, extensions = m_impl->extensions, showFiles = m_impl->showFiles](std::stop_token) {
        std::vector<Entry> entries = readFolder(path, extensions, showFiles);
        QMetaObject::invokeMethod(this, [this, generation, entries = std::move(entries)]() mutable {
            if (generation != m_impl->generation) {
                return;
            }
            beginResetModel();
            m_impl->entries = std::move(entries);
            m_impl->sort();
            endResetModel();
            m_impl->loading = false;
            emit loadingChanged();
            emit countChanged();
        });
    });
}

}  // namespace iv
