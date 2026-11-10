#include "ui/browse_context.h"
#include "ui/browse_context_p.h"

#include <QCollator>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QStringList>
#include <QTimeZone>
#include <QVariantMap>

#include <algorithm>
#include <ranges>
#include <stop_token>
#include <string_view>
#include <vector>

#include "core/image_details.h"
#include "core/thumbnail.h"
#include "ui/color_settings.h"

namespace iv {

namespace {

constexpr int kScanPriority = 100;
constexpr int kRefreshDelayMs = 200;  // copying many files changes the directory many times

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

}  // namespace

void BrowseContext::Impl::selectOnly(BrowseContext* owner, int index) {
    std::vector<char> selected(rowCount(), 0);
    if (index >= 0) {
        selected[static_cast<std::size_t>(index)] = 1;
    }
    anchor = index;
    apply(owner, selected);
}

void BrowseContext::Impl::apply(BrowseContext* owner, const std::vector<char>& selected) {
    if (files.setSelected(selected)) {
        emit owner->selectionChanged();
    }
}

void BrowseContext::Impl::moveCurrent(BrowseContext* owner, int index) {
    if (index != currentIndex) {
        currentIndex = index;
        currentName = files.name(index);
        emit owner->currentChanged();
    }
}

std::vector<char> BrowseContext::Impl::withRange(std::vector<char> base, int index) const {
    const int start = anchor >= 0 ? anchor : index;
    for (int row = std::min(start, index); row <= std::max(start, index); ++row) {
        base[static_cast<std::size_t>(row)] = 1;
    }
    return base;
}

void BrowseContext::Impl::open(BrowseContext* owner, const QString& path) {
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

void BrowseContext::Impl::travel(BrowseContext* owner, QList<HistoryEntry>& from, QList<HistoryEntry>& to, int steps) {
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

void BrowseContext::Impl::scan(BrowseContext* owner, const QStringList& toSelect) {
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

void BrowseContext::Impl::applyScan(BrowseContext* owner, const QList<FileEntry>& entries, const QStringList& toSelect) {
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

void BrowseContext::Impl::startJob(BrowseContext* owner, const QString& name, int total, FileJobRunner::Work work, Done done) {
    jobs.start(name, total, std::move(work), [owner, this, done = std::move(done)](const FileJobResult& result) {
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
}


BrowseContext::BrowseContext(QObject* parent) : QObject(parent), m_impl(std::make_unique<Impl>(this)) {
    connect(ColorSettings::instance(), &ColorSettings::changed, this, [this] { m_impl->files.thumbnailsChanged(); });
    m_impl->refreshTimer.setSingleShot(true);
    m_impl->refreshTimer.setInterval(kRefreshDelayMs);
    connect(&m_impl->refreshTimer, &QTimer::timeout, this, [this] { refresh(); });
    connect(&m_impl->watcher, &QFileSystemWatcher::directoryChanged, &m_impl->refreshTimer, qOverload<>(&QTimer::start));
    connect(&m_impl->jobs, &FileJobRunner::changed, this, &BrowseContext::jobChanged);
    connect(this, &BrowseContext::filesChanged, this, &BrowseContext::summaryChanged);
    connect(this, &BrowseContext::selectionChanged, this, &BrowseContext::summaryChanged);
}

BrowseContext::~BrowseContext() {
    // A running scan posts its result to this object, so it must finish first. The job runner
    // waits for its own.
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

bool BrowseContext::busy() const { return m_impl->jobs.busy(); }
QString BrowseContext::jobName() const { return m_impl->jobs.name(); }
int BrowseContext::jobDone() const { return m_impl->jobs.done(); }
int BrowseContext::jobTotal() const { return m_impl->jobs.total(); }


bool BrowseContext::isFolder(const QString& path) const {
    return QFileInfo(path).isDir();
}


QString BrowseContext::nativePath(const QString& path) const { return QDir::toNativeSeparators(path); }

QString BrowseContext::pathFromUrl(const QUrl& url) const { return url.toLocalFile(); }
QUrl BrowseContext::urlFromPath(const QString& path) const { return QUrl::fromLocalFile(path); }

}  // namespace iv
