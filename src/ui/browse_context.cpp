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
#include <vector>

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
    enum Role { NameRole = Qt::UserRole + 1, PathRole, ThumbnailRole, SelectedRole };

    using QAbstractListModel::QAbstractListModel;

    const QStringList& names() const { return m_names; }

    void reset(const QString& directory, const QStringList& names) {
        beginResetModel();
        m_directory = QDir(directory);
        m_names = names;
        m_selected.assign(static_cast<std::size_t>(names.size()), 0);
        endResetModel();
    }

    const std::vector<char>& selected() const { return m_selected; }

    // Replaces the selection, telling views only about the rows that differ.
    bool setSelected(const std::vector<char>& selected) {
        int first = -1;
        int last = -1;
        for (int row = 0; row < rowCount(); ++row) {
            const auto i = static_cast<std::size_t>(row);
            if ((selected[i] != 0) != (m_selected[i] != 0)) {
                first = first < 0 ? row : first;
                last = row;
            }
        }
        if (first < 0) {
            return false;
        }
        m_selected = selected;
        emit dataChanged(index(first), index(last), {SelectedRole});
        return true;
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
        case SelectedRole:
            return m_selected[static_cast<std::size_t>(index.row())] != 0;
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override {
        return {{NameRole, "name"}, {PathRole, "path"}, {ThumbnailRole, "thumbnail"}, {SelectedRole, "selected"}};
    }

private:
    QDir m_directory;
    QStringList m_names;
    std::vector<char> m_selected;
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
    int anchor = -1;  // where a range selection starts

    std::size_t rowCount() const { return files.selected().size(); }

    void selectOnly(BrowseContext* owner, int index) {
        std::vector<char> selected(rowCount(), 0);
        if (index >= 0) {
            selected[static_cast<std::size_t>(index)] = 1;
        }
        anchor = index;
        apply(owner, selected);
    }

    void apply(BrowseContext* owner, const std::vector<char>& selected) {
        if (files.setSelected(selected)) {
            emit owner->selectionChanged();
        }
    }

    void moveCurrent(BrowseContext* owner, int index) {
        if (index != currentIndex) {
            currentIndex = index;
            currentName = files.names().at(index);
            emit owner->currentChanged();
        }
    }

    // Marks the rows from the anchor to index, in addition to those in base.
    std::vector<char> withRange(std::vector<char> base, int index) const {
        const int start = anchor >= 0 ? anchor : index;
        for (int row = std::min(start, index); row <= std::max(start, index); ++row) {
            base[static_cast<std::size_t>(row)] = 1;
        }
        return base;
    }
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
    m_impl->anchor = -1;
    // A file shows immediately; the rest of the directory arrives when the scan finishes.
    m_impl->currentName = isDirectory ? QString() : info.fileName();
    emit directoryChanged();
    emit filesChanged();
    emit currentChanged();
    emit selectionChanged();

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
            m_impl->selectOnly(this, m_impl->currentIndex);
        });
    });
}

void BrowseContext::openUrl(const QUrl& url) { openPath(url.toLocalFile()); }

void BrowseContext::setCurrentIndex(int index) {
    if (index < 0 || index >= fileCount() || index == m_impl->currentIndex) {
        return;
    }
    m_impl->moveCurrent(this, index);
    m_impl->selectOnly(this, index);
}

void BrowseContext::clickFile(int index, bool ctrl, bool shift) {
    if (index < 0 || index >= fileCount()) {
        return;
    }
    if (shift) {
        m_impl->apply(this, m_impl->withRange(ctrl ? m_impl->files.selected() : std::vector<char>(m_impl->rowCount(), 0), index));
    } else if (ctrl) {
        std::vector<char> selected = m_impl->files.selected();
        char& row = selected[static_cast<std::size_t>(index)];
        row = row != 0 ? 0 : 1;
        m_impl->anchor = index;
        m_impl->apply(this, selected);
    } else {
        m_impl->selectOnly(this, index);
    }
    m_impl->moveCurrent(this, index);
}

void BrowseContext::extendSelectionTo(int index) {
    if (index < 0 || index >= fileCount()) {
        return;
    }
    if (m_impl->anchor < 0) {
        m_impl->anchor = m_impl->currentIndex;
    }
    m_impl->apply(this, m_impl->withRange(std::vector<char>(m_impl->rowCount(), 0), index));
    m_impl->moveCurrent(this, index);
}

void BrowseContext::selectAll() { m_impl->apply(this, std::vector<char>(m_impl->rowCount(), 1)); }

void BrowseContext::clearSelection() { m_impl->apply(this, std::vector<char>(m_impl->rowCount(), 0)); }

int BrowseContext::selectedCount() const {
    const auto& selected = m_impl->files.selected();
    return static_cast<int>(std::ranges::count_if(selected, [](char value) { return value != 0; }));
}

QStringList BrowseContext::selectedPaths() const {
    QStringList paths;
    const auto& selected = m_impl->files.selected();
    const QDir directory(m_impl->directory);
    for (std::size_t row = 0; row < selected.size(); ++row) {
        if (selected[row] != 0) {
            paths << directory.filePath(m_impl->files.names().at(static_cast<qsizetype>(row)));
        }
    }
    return paths;
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
