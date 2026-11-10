#include "ui/browse_context.h"

#include <QAbstractListModel>
#include <QClipboard>
#include <QCollator>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QList>
#include <QSet>
#include <QStringList>
#include <QTimeZone>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <ranges>
#include <cstdint>
#include <functional>
#include <limits>
#include <stop_token>
#include <string_view>
#include <vector>

#include "core/image_details.h"
#include "core/image_source.h"
#include "core/jpeg_transform.h"
#include "core/thumbnail.h"
#include "core/work_queue.h"
#include "ui/color_settings.h"
#include "ui/edited_image.h"
#include "ui/file_operations.h"
#include "ui/qimage_bridge.h"
#include "ui/thumbnail_provider.h"

namespace iv {

namespace {

constexpr int kScanPriority = 100;
constexpr int kJobPriority = 100;
constexpr int kRefreshDelayMs = 200;  // copying many files changes the directory many times

// A folder visited, and the image that was current in it.
struct HistoryEntry {
    QString directory;
    QString currentName;
};

struct FileEntry {
    QString name;
    qint64 size = 0;
    qint64 modified = 0;
};

QStringList imageNameFilters() {
    QStringList filters;
    for (const std::string_view extension : supportedExtensions()) {
        filters << QStringLiteral("*.") + QString::fromLatin1(extension);
    }
    return filters;
}

QList<FileEntry> scanDirectory(const QString& directory, BrowseContext::SortKey key, bool descending) {
    QList<FileEntry> entries;
    const QFileInfoList infos = QDir(directory).entryInfoList(imageNameFilters(), QDir::Files | QDir::NoDotAndDotDot);
    entries.reserve(infos.size());
    for (const QFileInfo& info : infos) {
        entries.append({info.fileName(), info.size(), info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch()});
    }

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    const auto byName = [&](const FileEntry& a, const FileEntry& b) { return collator.compare(a.name, b.name) < 0; };
    const auto suffix = [](const FileEntry& entry) { return entry.name.mid(entry.name.lastIndexOf(QLatin1Char('.')) + 1).toLower(); };
    std::ranges::sort(entries, [&](const FileEntry& a, const FileEntry& b) {
        switch (key) {
        case BrowseContext::SortByDate:
            if (a.modified != b.modified) {
                return a.modified < b.modified;
            }
            break;
        case BrowseContext::SortBySize:
            if (a.size != b.size) {
                return a.size < b.size;
            }
            break;
        case BrowseContext::SortByType: {
            const int order = suffix(a).compare(suffix(b));
            if (order != 0) {
                return order < 0;
            }
            break;
        }
        case BrowseContext::SortByName:
            break;
        }
        return byName(a, b);
    });
    if (descending) {
        std::ranges::reverse(entries);
    }
    return entries;
}

class FileListModel final : public QAbstractListModel {
public:
    enum Role { NameRole = Qt::UserRole + 1, PathRole, ThumbnailRole, SelectedRole };

    using QAbstractListModel::QAbstractListModel;

    const QString& name(int row) const { return m_entries.at(row).name; }
    qint64 size(int row) const { return m_entries.at(row).size; }

    int find(const QString& name) const {
        for (int row = 0; row < rowCount(); ++row) {
            if (m_entries.at(row).name == name) {
                return row;
            }
        }
        return -1;
    }

    void reset(const QString& directory, const QList<FileEntry>& entries) {
        beginResetModel();
        m_directory = QDir(directory);
        m_entries = entries;
        m_selected.assign(static_cast<std::size_t>(entries.size()), 0);
        endResetModel();
    }

    // Takes a new scan of the same directory, keeping the selection by name. Only the rows
    // that differ change, so that a view stays where it was scrolled to. Returns whether any did.
    bool update(const QList<FileEntry>& entries) {
        if (m_entries.isEmpty()) {
            if (entries.isEmpty()) {
                return false;
            }
            reset(m_directory.path(), entries);
            return true;
        }
        QSet<QString> incoming;
        for (const FileEntry& entry : entries) {
            incoming.insert(entry.name);
        }
        bool changed = false;
        for (int row = rowCount() - 1; row >= 0; --row) {
            if (!incoming.contains(m_entries.at(row).name)) {
                beginRemoveRows({}, row, row);
                m_entries.removeAt(row);
                m_selected.erase(m_selected.begin() + row);
                endRemoveRows();
                changed = true;
            }
        }

        QSet<QString> present;
        for (const FileEntry& entry : std::as_const(m_entries)) {
            present.insert(entry.name);
        }
        int kept = 0;
        bool sameOrder = true;
        for (const FileEntry& entry : entries) {
            if (present.contains(entry.name) && m_entries.at(kept++).name != entry.name) {
                sameOrder = false;
                break;
            }
        }
        if (!sameOrder) {
            replaceReordered(entries);
            return true;
        }

        for (int row = 0; row < entries.size(); ++row) {
            const FileEntry& entry = entries.at(row);
            if (row >= rowCount() || m_entries.at(row).name != entry.name) {
                beginInsertRows({}, row, row);
                m_entries.insert(row, entry);
                m_selected.insert(m_selected.begin() + row, 0);
                endInsertRows();
                changed = true;
            } else if (m_entries.at(row).modified != entry.modified || m_entries.at(row).size != entry.size) {
                m_entries[row] = entry;
                emit dataChanged(index(row), index(row), {ThumbnailRole});
                changed = true;
            }
        }
        return changed;
    }

    const std::vector<char>& selected() const { return m_selected; }

    void thumbnailsChanged() {
        if (rowCount() > 0) {
            emit dataChanged(index(0), index(rowCount() - 1), {ThumbnailRole});
        }
    }

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
        return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
    }

    QVariant data(const QModelIndex& index, int role) const override {
        if (!checkIndex(index, CheckIndexOption::IndexIsValid)) {
            return {};
        }
        const FileEntry& entry = m_entries.at(index.row());
        switch (role) {
        case Qt::DisplayRole:
        case NameRole:
            return entry.name;
        case PathRole:
            return m_directory.filePath(entry.name);
        case ThumbnailRole:
            return ThumbnailProvider::urlForPath(m_directory.filePath(entry.name), entry.modified);
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
    void replaceReordered(const QList<FileEntry>& entries) {
        QSet<QString> selectedNames;
        for (int row = 0; row < rowCount(); ++row) {
            if (m_selected[static_cast<std::size_t>(row)] != 0) {
                selectedNames.insert(m_entries.at(row).name);
            }
        }
        reset(m_directory.path(), entries);
        for (int row = 0; row < rowCount(); ++row) {
            m_selected[static_cast<std::size_t>(row)] = selectedNames.contains(m_entries.at(row).name) ? 1 : 0;
        }
    }

    QDir m_directory;
    QList<FileEntry> m_entries;
    std::vector<char> m_selected;
};

}  // namespace

struct BrowseContext::Impl {
    explicit Impl(BrowseContext* owner) : files(owner), watcher(owner), refreshTimer(owner) {}

    using Work = std::function<FileJobResult(std::stop_token, const JobProgress&)>;
    using Done = std::function<void(const FileJobResult&)>;

    WorkQueue& queue = WorkQueue::global();
    WorkQueue::GroupId group = queue.createGroup();
    WorkQueue::GroupId jobGroup = queue.createGroup();  // survives moving to another directory
    std::uint64_t scanGeneration = 0;
    QString directory;
    QString currentName;
    FileListModel files;
    QFileSystemWatcher watcher;
    QTimer refreshTimer;
    SortKey sortKey = SortByName;
    bool sortDescending = false;
    int currentIndex = -1;
    int anchor = -1;  // where a range selection starts
    bool busy = false;
    QString jobName;
    int jobDone = 0;
    int jobTotal = 0;
    std::stop_source jobStop;
    QList<HistoryEntry> backStack;
    QList<HistoryEntry> forwardStack;
    int subfolderCount = 0;

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
            currentName = files.name(index);
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

    // Shows a file or directory, without recording it in the history.
    void open(BrowseContext* owner, const QString& path) {
        const QFileInfo info(path);
        const bool isDirectory = info.isDir();
        const QString newDirectory = isDirectory ? info.absoluteFilePath() : info.absolutePath();

        if (!directory.isEmpty()) {
            watcher.removePath(directory);
        }
        refreshTimer.stop();
        directory = newDirectory;
        watcher.addPath(directory);
        files.reset(directory, {});
        currentIndex = -1;
        anchor = -1;
        subfolderCount = 0;
        // A file shows immediately; the rest of the directory arrives when the scan finishes.
        currentName = isDirectory ? QString() : info.fileName();
        emit owner->directoryChanged();
        emit owner->filesChanged();
        emit owner->currentChanged();
        emit owner->selectionChanged();

        scan(owner);
    }

    // Moves through the history: takes the entry so many steps away on one stack, counting those
    // that still exist, and leaves where we are and what it passed on the other.
    void travel(BrowseContext* owner, QList<HistoryEntry>& from, QList<HistoryEntry>& to, int steps = 1) {
        QList<HistoryEntry> passed;
        while (!from.isEmpty()) {
            const HistoryEntry entry = from.takeLast();
            if (!QFileInfo(entry.directory).isDir()) {
                continue;
            }
            if (--steps > 0 && !from.isEmpty()) {
                passed.append(entry);
                continue;
            }
            to.append({directory, currentName});
            to.append(passed);
            const QString file = QDir(entry.directory).filePath(entry.currentName);
            open(owner, !entry.currentName.isEmpty() && QFileInfo::exists(file) ? file : entry.directory);
            break;
        }
        emit owner->historyChanged();
    }

    // Scans the directory. Names in toSelect, if present then, become the selection.
    void scan(BrowseContext* owner, const QStringList& toSelect = {}) {
        queue.cancelGroup(group);
        const std::uint64_t generation = ++scanGeneration;
        queue.submit(group, kScanPriority, [owner, this, generation, toSelect, dir = directory, key = sortKey, down = sortDescending](std::stop_token) {
            QList<FileEntry> entries = scanDirectory(dir, key, down);
            const int subfolders = static_cast<int>(QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size());
            QMetaObject::invokeMethod(owner, [owner, this, generation, toSelect, subfolders, entries = std::move(entries)] {
                if (generation == scanGeneration) {
                    subfolderCount = subfolders;
                    applyScan(owner, entries, toSelect);
                    emit owner->summaryChanged();  // the folders can change on their own
                }
            });
        });
    }

    void applyScan(BrowseContext* owner, const QList<FileEntry>& entries, const QStringList& toSelect) {
        const int previousIndex = currentIndex;
        const QString previousName = currentName;
        const bool hadFiles = rowCount() > 0;
        const bool hadSelection = owner->selectedCount() > 0;
        const QString anchorName = anchor >= 0 ? files.name(anchor) : QString();

        const bool changed = files.update(entries);

        const int rows = static_cast<int>(rowCount());
        const auto selectable = [&] {
            QStringList present;
            for (const QString& name : toSelect) {
                if (files.find(name) >= 0) {
                    present << name;
                }
            }
            return present;
        }();

        if (!selectable.isEmpty()) {
            currentName = selectable.front();
        } else if (currentName.isEmpty()) {
            currentName = rows > 0 ? files.name(0) : QString();
        } else if (previousIndex >= 0 && files.find(currentName) < 0) {
            // It is gone, so the next one has moved into its place.
            currentName = rows > 0 ? files.name(std::min(previousIndex, rows - 1)) : QString();
        }
        currentIndex = files.find(currentName);
        anchor = anchorName.isEmpty() ? -1 : files.find(anchorName);

        if (changed || !hadFiles) {
            emit owner->filesChanged();
        }
        if (currentIndex != previousIndex || currentName != previousName || !hadFiles) {
            emit owner->currentChanged();
        }

        if (!selectable.isEmpty()) {
            std::vector<char> selected(rowCount(), 0);
            for (const QString& name : selectable) {
                selected[static_cast<std::size_t>(files.find(name))] = 1;
            }
            anchor = currentIndex;
            apply(owner, selected);
        } else if (!hadFiles || (hadSelection && owner->selectedCount() == 0)) {
            selectOnly(owner, currentIndex);
        }
        if (changed) {
            emit owner->selectionChanged();  // the rows may have moved under it
        }

        if (hadFiles && rows == 0) {
            emit owner->emptied();
        }
    }

    // Starts a job unless one is running. done runs on the main thread when it ends, and by
    // default the directory is scanned and files new in it are selected.
    void startJob(BrowseContext* owner, const QString& name, int total, Work work, Done done = {}) {
        if (busy) {
            return;
        }
        busy = true;
        jobName = name;
        jobDone = 0;
        jobTotal = total;
        jobStop = std::stop_source();
        emit owner->jobChanged();
        queue.submit(jobGroup, kJobPriority, [owner, this, work = std::move(work), done = std::move(done), stop = jobStop.get_token()](std::stop_token) {
            const JobProgress progress = [owner, this](int count) {
                QMetaObject::invokeMethod(owner, [owner, this, count] {
                    jobDone = count;
                    emit owner->jobChanged();
                });
            };
            FileJobResult result = work(stop, progress);
            QMetaObject::invokeMethod(owner, [owner, this, done, result = std::move(result)] {
                busy = false;
                emit owner->jobChanged();
                if (done) {
                    done(result);
                } else {
                    QStringList toSelect;
                    for (const QString& path : result.created) {
                        const QFileInfo info(path);
                        if (info.dir() == QDir(directory)) {
                            toSelect << info.fileName();
                        }
                    }
                    scan(owner, toSelect);
                }
                emit owner->operationFinished(result.problems);
            });
        });
    }
};

BrowseContext::BrowseContext(QObject* parent) : QObject(parent), m_impl(std::make_unique<Impl>(this)) {
    connect(ColorSettings::instance(), &ColorSettings::changed, this, [this] { m_impl->files.thumbnailsChanged(); });
    m_impl->refreshTimer.setSingleShot(true);
    m_impl->refreshTimer.setInterval(kRefreshDelayMs);
    connect(&m_impl->refreshTimer, &QTimer::timeout, this, [this] { refresh(); });
    connect(&m_impl->watcher, &QFileSystemWatcher::directoryChanged, &m_impl->refreshTimer, qOverload<>(&QTimer::start));
    connect(this, &BrowseContext::filesChanged, this, &BrowseContext::summaryChanged);
    connect(this, &BrowseContext::selectionChanged, this, &BrowseContext::summaryChanged);
}

BrowseContext::~BrowseContext() {
    // Running work posts its result to this object, so it must finish first.
    m_impl->jobStop.request_stop();
    for (const WorkQueue::GroupId group : {m_impl->group, m_impl->jobGroup}) {
        m_impl->queue.cancelGroup(group);
        m_impl->queue.waitGroup(group);
    }
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
    const QString target = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (!m_impl->directory.isEmpty() && target != m_impl->directory) {
        m_impl->backStack.append({m_impl->directory, m_impl->currentName});
        m_impl->forwardStack.clear();
        emit historyChanged();
    }
    m_impl->open(this, path);
}

bool BrowseContext::canGoBack() const { return !m_impl->backStack.isEmpty(); }
bool BrowseContext::canGoForward() const { return !m_impl->forwardStack.isEmpty(); }
bool BrowseContext::canGoUp() const { return !m_impl->directory.isEmpty() && !QDir(m_impl->directory).isRoot(); }

void BrowseContext::goBack() { m_impl->travel(this, m_impl->backStack, m_impl->forwardStack); }

QStringList BrowseContext::backFolders() const {
    QStringList folders;
    for (const HistoryEntry& entry : std::views::reverse(m_impl->backStack)) {
        if (QFileInfo(entry.directory).isDir()) {
            folders << entry.directory;
        }
    }
    return folders;
}

void BrowseContext::goBackBy(int steps) { m_impl->travel(this, m_impl->backStack, m_impl->forwardStack, steps); }
void BrowseContext::goForward() { m_impl->travel(this, m_impl->forwardStack, m_impl->backStack); }

void BrowseContext::goUp() {
    if (canGoUp()) {
        openPath(QFileInfo(m_impl->directory).absolutePath());
    }
}

QString BrowseContext::dataSizeText(double bytes) const {
    return QString::fromStdString(shortDataSize(bytes > 0 ? static_cast<std::uint64_t>(bytes) : 0));
}

QString BrowseContext::summary() const {
    if (m_impl->directory.isEmpty()) {
        return {};
    }
    const auto sizeText = [](qint64 bytes) { return QString::fromStdString(shortDataSize(static_cast<std::uint64_t>(bytes))); };
    const FileListModel& files = m_impl->files;
    qint64 total = 0;
    qint64 selectedBytes = 0;
    int selected = 0;
    for (int row = 0; row < files.rowCount(); ++row) {
        total += files.size(row);
        if (files.selected()[static_cast<std::size_t>(row)] != 0) {
            selectedBytes += files.size(row);
            ++selected;
        }
    }
    const int folders = m_impl->subfolderCount;
    QString text = tr("%1 %2 | %3 %4 (%5)")
                       .arg(folders)
                       .arg(folders == 1 ? tr("directory") : tr("directories"))
                       .arg(files.rowCount())
                       .arg(files.rowCount() == 1 ? tr("file") : tr("files"), sizeText(total));
    if (selected > 0) {
        text += tr(" | %1 selected (%2)").arg(selected).arg(sizeText(selectedBytes));
    }
    return text;
}

QString BrowseContext::openTyped(const QString& text) {
    QString typed = text.section(QLatin1Char('\n'), 0, 0).trimmed();
    if (typed.size() >= 2 && (typed.front() == QLatin1Char('"') || typed.front() == QLatin1Char('\'')) && typed.back() == typed.front()) {
        typed = typed.mid(1, typed.size() - 2);
    }
    if (typed.isEmpty()) {
        return tr("Enter a folder or file.");
    }
    if (typed.startsWith(QStringLiteral("file:"))) {
        const QUrl url(typed);
        if (!url.isLocalFile()) {
            return tr("Only local folders can be opened.");
        }
        typed = url.toLocalFile();
    } else if (typed.contains(QStringLiteral("://"))) {
        return tr("Only local folders can be opened.");
    }
    typed = QDir::fromNativeSeparators(typed);
    if (typed == QStringLiteral("~") || typed.startsWith(QStringLiteral("~/"))) {
        typed = QDir::homePath() + typed.mid(1);
    }
    const QFileInfo info(QDir(m_impl->directory).absoluteFilePath(typed));
    if (!info.exists()) {
        return tr("%1 does not exist.").arg(QDir::toNativeSeparators(info.absoluteFilePath()));
    }
    openPath(QDir::cleanPath(info.absoluteFilePath()));
    return {};
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
            paths << directory.filePath(m_impl->files.name(static_cast<int>(row)));
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

void BrowseContext::refresh() {
    if (!m_impl->directory.isEmpty()) {
        m_impl->scan(this);
    }
}

BrowseContext::SortKey BrowseContext::sortKey() const { return m_impl->sortKey; }

void BrowseContext::setSortKey(SortKey key) {
    if (key != m_impl->sortKey) {
        m_impl->sortKey = key;
        emit sortChanged();
        refresh();
    }
}

bool BrowseContext::sortDescending() const { return m_impl->sortDescending; }

void BrowseContext::setSortDescending(bool descending) {
    if (descending != m_impl->sortDescending) {
        m_impl->sortDescending = descending;
        emit sortChanged();
        refresh();
    }
}

bool BrowseContext::busy() const { return m_impl->busy; }
QString BrowseContext::jobName() const { return m_impl->jobName; }
int BrowseContext::jobDone() const { return m_impl->jobDone; }
int BrowseContext::jobTotal() const { return m_impl->jobTotal; }

namespace {

ConflictPolicy toPolicy(BrowseContext::Conflict conflict) {
    switch (conflict) {
    case BrowseContext::ReplaceExisting:
        return ConflictPolicy::Replace;
    case BrowseContext::KeepBoth:
        return ConflictPolicy::KeepBoth;
    case BrowseContext::SkipExisting:
        break;
    }
    return ConflictPolicy::Skip;
}

// The path after a folder is renamed, for a path in or below that folder.
QString afterFolderRename(const QString& path, const QString& oldFolder, const QString& newFolder) {
    return path == oldFolder ? newFolder : newFolder + path.mid(oldFolder.size());
}

bool isInFolder(const QString& path, const QString& folder) {
    return path == folder || path.startsWith(folder + QLatin1Char('/'));
}

}  // namespace

void BrowseContext::trash(const QStringList& paths) {
    if (paths.isEmpty()) {
        return;
    }
    m_impl->startJob(this, tr("Moving to trash"), static_cast<int>(paths.size()), [paths](std::stop_token stop, const JobProgress& progress) {
        return trashFiles(paths, stop, progress);
    });
}

void BrowseContext::deletePermanently(const QStringList& paths) {
    if (paths.isEmpty()) {
        return;
    }
    m_impl->startJob(this, tr("Deleting"), static_cast<int>(paths.size()), [paths](std::stop_token stop, const JobProgress& progress) {
        return deleteFiles(paths, stop, progress);
    });
}

void BrowseContext::copyTo(const QStringList& paths, const QString& folder, Conflict conflict) {
    if (paths.isEmpty()) {
        return;
    }
    m_impl->startJob(this, tr("Copying"), static_cast<int>(paths.size()),
                     [paths, folder, policy = toPolicy(conflict)](std::stop_token stop, const JobProgress& progress) {
                         return copyFiles(paths, folder, policy, stop, progress);
                     });
}

void BrowseContext::moveTo(const QStringList& paths, const QString& folder, Conflict conflict) {
    if (paths.isEmpty()) {
        return;
    }
    m_impl->startJob(this, tr("Moving"), static_cast<int>(paths.size()),
                     [paths, folder, policy = toPolicy(conflict)](std::stop_token stop, const JobProgress& progress) {
                         return moveFiles(paths, folder, policy, stop, progress);
                     });
}

int BrowseContext::conflictCount(const QStringList& paths, const QString& folder) const { return countConflicts(paths, folder); }

QString BrowseContext::newFileProblem(const QString& folder, const QString& name) const {
    if (QString problem = fileNameProblem(name); !problem.isEmpty()) {
        return problem;
    }
    if (QFileInfo::exists(QDir(folder).filePath(name))) {
        return tr("%1 already exists.").arg(name);
    }
    return {};
}

QString BrowseContext::rename(const QString& path, const QString& newName) {
    const QFileInfo info(path);
    if (newName == info.fileName()) {
        return {};
    }
    if (QString problem = fileNameProblem(newName); !problem.isEmpty()) {
        return problem;
    }
    // Only the case differs: on some file systems that is the same name, and still a rename.
    const bool caseOnly = newName.compare(info.fileName(), Qt::CaseInsensitive) == 0;
    if (!caseOnly && QFileInfo::exists(info.dir().filePath(newName))) {
        return tr("%1 already exists.").arg(newName);
    }
    const bool isFolder = info.isDir();
    const QString oldPath = info.absoluteFilePath();
    const QString newPath = info.dir().filePath(newName);
    Impl::Done done;
    if (isFolder) {
        done = [this, oldPath, newPath](const FileJobResult& result) {
            if (!result.problems.isEmpty()) {
                return;
            }
            for (QList<HistoryEntry>* stack : {&m_impl->backStack, &m_impl->forwardStack}) {
                for (HistoryEntry& entry : *stack) {
                    if (isInFolder(entry.directory, oldPath)) {
                        entry.directory = afterFolderRename(entry.directory, oldPath, newPath);
                    }
                }
            }
            if (isInFolder(m_impl->directory, oldPath)) {
                const QString directory = afterFolderRename(m_impl->directory, oldPath, newPath);
                const QString name = m_impl->currentName;
                m_impl->open(this, name.isEmpty() ? directory : QDir(directory).filePath(name));
            }
        };
    }
    m_impl->startJob(this, tr("Renaming"), 1, [path, newName](std::stop_token, const JobProgress&) { return renamePath(path, newName); },
                     std::move(done));
    return {};
}

QString BrowseContext::createFolder(const QString& parent, const QString& name) {
    if (QString problem = fileNameProblem(name); !problem.isEmpty()) {
        return problem;
    }
    if (QFileInfo::exists(QDir(parent).filePath(name))) {
        return tr("%1 already exists.").arg(name);
    }
    m_impl->startJob(this, tr("Creating folder"), 1, [parent, name](std::stop_token, const JobProgress&) { return iv::createFolder(parent, name); },
                     [](const FileJobResult&) {});
    return {};
}

void BrowseContext::trashFolder(const QString& path) {
    const QString folder = QFileInfo(path).absoluteFilePath();
    const QDir directory(folder);
    if (directory.isRoot() || isInFolder(QDir::homePath(), folder)) {
        emit operationFinished({folder + tr(": this folder cannot be moved to the trash")});
        return;
    }
    if (isInFolder(m_impl->directory, folder)) {
        // Leave it first, as nothing should be reading from it any more.
        openPath(QFileInfo(folder).absolutePath());
    }
    m_impl->startJob(this, tr("Moving to trash"), 1, [folder](std::stop_token stop, const JobProgress& progress) {
        return trashFiles({folder}, stop, progress);
    }, [](const FileJobResult&) {});
}

bool BrowseContext::allJpeg(const QStringList& paths) const {
    return !paths.isEmpty() && std::ranges::all_of(paths, [](const QString& path) {
        return hasJpegExtension(QFileInfo(path).filesystemFilePath());
    });
}

void BrowseContext::editJpegs(const QStringList& paths, JpegOperation operation, bool trimEdges) {
    if (paths.isEmpty()) {
        return;
    }
    static constexpr JpegEdit kEdits[] = {JpegEdit::RotateClockwise, JpegEdit::RotateCounterclockwise, JpegEdit::Rotate180,
                                          JpegEdit::FlipHorizontal,  JpegEdit::FlipVertical,           JpegEdit::ApplyOrientation};
    const JpegEdit edit = kEdits[operation];
    m_impl->startJob(this, operation == ApplyOrientation ? tr("Applying orientation") : tr("Rotating"),
                     static_cast<int>(paths.size()),
                     [paths, edit, trimEdges](std::stop_token stop, const JobProgress& progress) {
                         return editJpegFiles(paths, edit, trimEdges, stop, progress);
                     },
                     [this](const FileJobResult& result) {
                         m_impl->scan(this);
                         if (!result.created.isEmpty()) {
                             emit filesEdited(result.created);
                         }
                     });
}

void BrowseContext::convertToJxl(const QStringList& paths, bool trashOriginals) {
    if (paths.isEmpty()) {
        return;
    }
    m_impl->startJob(this, tr("Converting to JPEG XL"), static_cast<int>(paths.size()),
                     [paths, trashOriginals](std::stop_token stop, const JobProgress& progress) {
                         return convertJpegFilesToJxl(paths, trashOriginals, stop, progress);
                     });
}

bool BrowseContext::canConvertToJxl() const { return iv::canConvertToJxl(); }

QString BrowseContext::saveFormat(const QString& path) const { return saveFormatFor(path); }

QVariantList BrowseContext::saveTypes() const {
    struct Type {
        const char* label;
        QStringList extensions;
    };
    const Type kTypes[] = {
        {"JPEG", {QStringLiteral("jpg"), QStringLiteral("jpeg")}}, {"PNG", {QStringLiteral("png")}},
        {"TIFF", {QStringLiteral("tif"), QStringLiteral("tiff")}}, {"WebP", {QStringLiteral("webp")}},
        {"AVIF", {QStringLiteral("avif")}},                        {"JPEG XL", {QStringLiteral("jxl")}},
    };
    QVariantList types;
    for (const Type& type : kTypes) {
        if (!saveFormatFor(QStringLiteral("x.") + type.extensions.first()).isEmpty()) {
            types << QVariantMap{{QStringLiteral("label"), QLatin1String(type.label)},
                                 {QStringLiteral("extensions"), type.extensions}};
        }
    }
    return types;
}

void BrowseContext::saveAs(const QString& source, int page, const QString& target, const QVariantMap& options,
                           const QVariant& edited) {
    if (source.isEmpty() || target.isEmpty()) {
        return;
    }
    auto image = edited.value<std::shared_ptr<ImageSource>>();
    m_impl->startJob(this, tr("Saving"), 1,
                     [source, page, target, options, image](std::stop_token stop, const JobProgress&) {
                         return saveImageAs(source, page, target, options, stop, image);
                     },
                     [this, edited](const FileJobResult& result) {
                         if (edited.isValid() && !result.created.isEmpty()) {
                             emit editedImageSaved(edited);
                         }
                         QStringList toSelect;
                         for (const QString& path : result.created) {
                             if (QFileInfo(path).dir() == QDir(m_impl->directory)) {
                                 toSelect << QFileInfo(path).fileName();
                             }
                         }
                         m_impl->scan(this, toSelect);
                         // A file that was replaced may be on show.
                         if (!result.created.isEmpty()) {
                             emit filesEdited(result.created);
                         }
                     });
}

void BrowseContext::cancelJob() { m_impl->jobStop.request_stop(); }

bool BrowseContext::isFolder(const QString& path) const {
    return QFileInfo(path).isDir();
}

void BrowseContext::pasteImage() {
    const QImage image = QGuiApplication::clipboard()->image();
    if (image.isNull()) {
        emit operationFinished({tr("The clipboard holds no image.")});
        return;
    }
    m_impl->startJob(this, tr("Pasting"), 1, [image, folder = m_impl->directory](std::stop_token, const JobProgress&) {
        return saveNewImage(image, folder, QDateTime::currentDateTime());
    });
}

QString BrowseContext::nativePath(const QString& path) const { return QDir::toNativeSeparators(path); }

QString BrowseContext::pathFromUrl(const QUrl& url) const { return url.toLocalFile(); }
QUrl BrowseContext::urlFromPath(const QString& path) const { return QUrl::fromLocalFile(path); }

}  // namespace iv
