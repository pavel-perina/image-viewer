#include "ui/browse_context.h"

#include <QAbstractListModel>
#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <cstdint>
#include <string_view>

#include "core/image_source.h"
#include "core/work_queue.h"
#include "ui/thumbnail_provider.h"

namespace iv {

namespace {

constexpr int kScanPriority = 100;

QStringList imageNameFilters() {
    QStringList filters;
    for (const std::string_view extension : supportedExtensions()) {
        filters << QStringLiteral("*.") + QString::fromLatin1(extension);
    }
    return filters;
}

QStringList scanDirectory(const QString& directory) {
    QStringList names = QDir(directory).entryList(imageNameFilters(), QDir::Files | QDir::NoDotAndDotDot);
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::ranges::sort(names, collator);
    return names;
}

class FileListModel final : public QAbstractListModel {
public:
    enum Role { NameRole = Qt::UserRole + 1, PathRole, ThumbnailRole };

    using QAbstractListModel::QAbstractListModel;

    const QStringList& names() const { return m_names; }

    void reset(const QString& directory, const QStringList& names) {
        beginResetModel();
        m_directory = QDir(directory);
        m_names = names;
        endResetModel();
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : static_cast<int>(m_names.size());
    }

    QVariant data(const QModelIndex& index, int role) const override {
        if (!checkIndex(index, CheckIndexOption::IndexIsValid)) {
            return {};
        }
        const QString& name = m_names.at(index.row());
        switch (role) {
        case Qt::DisplayRole:
        case NameRole:
            return name;
        case PathRole:
            return m_directory.filePath(name);
        case ThumbnailRole:
            return ThumbnailProvider::urlForPath(m_directory.filePath(name));
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override {
        return {{NameRole, "name"}, {PathRole, "path"}, {ThumbnailRole, "thumbnail"}};
    }

private:
    QDir m_directory;
    QStringList m_names;
};

}  // namespace

struct BrowseContext::Impl {
    explicit Impl(BrowseContext* owner) : files(owner) {}

    WorkQueue& queue = WorkQueue::global();
    WorkQueue::GroupId group = queue.createGroup();
    std::uint64_t scanGeneration = 0;
    QString directory;
    QString currentName;
    FileListModel files;
    int currentIndex = -1;
};

BrowseContext::BrowseContext(QObject* parent) : QObject(parent), m_impl(std::make_unique<Impl>(this)) {}

BrowseContext::~BrowseContext() {
    // A running scan posts its result to this object, so it must finish first.
    m_impl->queue.cancelGroup(m_impl->group);
    m_impl->queue.waitGroup(m_impl->group);
}

QString BrowseContext::directory() const { return m_impl->directory; }

QVariantList BrowseContext::breadcrumbs() const {
    QVariantList crumbs;
    if (m_impl->directory.isEmpty()) {
        return crumbs;
    }
    const auto addCrumb = [&](const QString& name, const QString& path) {
        crumbs.append(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("path"), path}});
    };

    const QString clean = QDir::cleanPath(m_impl->directory);
    QString path = QDir(clean).isRoot() ? clean : clean.left(clean.indexOf(QLatin1Char('/')) + 1);
    addCrumb(path, path);
    for (const QString& part : clean.mid(path.size()).split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        path = QDir(path).filePath(part);
        addCrumb(part, path);
    }
    return crumbs;
}

QAbstractItemModel* BrowseContext::files() const { return &m_impl->files; }

QString BrowseContext::currentPath() const {
    return m_impl->currentName.isEmpty() ? QString() : QDir(m_impl->directory).filePath(m_impl->currentName);
}

QString BrowseContext::currentName() const { return m_impl->currentName; }
int BrowseContext::currentIndex() const { return m_impl->currentIndex; }
int BrowseContext::fileCount() const { return m_impl->files.rowCount(); }
bool BrowseContext::hasNext() const { return m_impl->currentIndex + 1 < fileCount(); }
bool BrowseContext::hasPrevious() const { return m_impl->currentIndex > 0; }

void BrowseContext::openPath(const QString& path) {
    const QFileInfo info(path);
    const bool isDirectory = info.isDir();
    const QString directory = isDirectory ? info.absoluteFilePath() : info.absolutePath();

    m_impl->queue.cancelGroup(m_impl->group);
    const std::uint64_t generation = ++m_impl->scanGeneration;
    m_impl->directory = directory;
    m_impl->files.reset(directory, {});
    m_impl->currentIndex = -1;
    // A file shows immediately; the rest of the directory arrives when the scan finishes.
    m_impl->currentName = isDirectory ? QString() : info.fileName();
    emit directoryChanged();
    emit filesChanged();
    emit currentChanged();

    m_impl->queue.submit(m_impl->group, kScanPriority, [this, directory, generation](std::stop_token) {
        QStringList files = scanDirectory(directory);
        QMetaObject::invokeMethod(this, [this, generation, files = std::move(files)] {
            if (generation != m_impl->scanGeneration) {
                return;
            }
            m_impl->files.reset(m_impl->directory, files);
            if (m_impl->currentName.isEmpty() && !files.isEmpty()) {
                m_impl->currentName = files.front();
            }
            m_impl->currentIndex = static_cast<int>(files.indexOf(m_impl->currentName));
            emit filesChanged();
            emit currentChanged();
        });
    });
}

void BrowseContext::openUrl(const QUrl& url) { openPath(url.toLocalFile()); }

void BrowseContext::setCurrentIndex(int index) {
    if (index < 0 || index >= fileCount() || index == m_impl->currentIndex) {
        return;
    }
    m_impl->currentIndex = index;
    m_impl->currentName = m_impl->files.names().at(index);
    emit currentChanged();
}

void BrowseContext::next() {
    if (hasNext()) {
        setCurrentIndex(m_impl->currentIndex + 1);
    }
}

void BrowseContext::previous() {
    if (hasPrevious()) {
        setCurrentIndex(m_impl->currentIndex - 1);
    }
}

}  // namespace iv
