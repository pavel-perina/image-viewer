#pragma once

// What the parts of BrowseContext share: the browsing in browse_context.cpp, the file jobs in
// browse_context_files.cpp.

#include <QFileSystemWatcher>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <vector>

#include "core/work_queue.h"
#include "ui/browse_context.h"
#include "ui/file_job_runner.h"
#include "ui/file_list_model.h"

namespace iv {

// A folder visited, and the image that was current in it.
struct HistoryEntry {
    QString directory;
    QString currentName;
};

struct BrowseContext::Impl {
    explicit Impl(BrowseContext* owner) : files(owner), watcher(owner), refreshTimer(owner), jobs(owner) {}

    using Done = std::function<void(const FileJobResult&)>;

    WorkQueue& queue = WorkQueue::global();
    WorkQueue::GroupId group = queue.createGroup();
    std::uint64_t scanGeneration = 0;
    QString directory;
    QString currentName;
    FileListModel files;
    QFileSystemWatcher watcher;
    QTimer refreshTimer;
    SortKey sortKey = SortByName;
    bool sortDescending = false;
    bool showHiddenFolders = false;
    int currentIndex = -1;
    int anchor = -1;  // where a range selection starts
    QList<HistoryEntry> backStack;
    QList<HistoryEntry> forwardStack;
    int subfolderCount = 0;
    FileJobRunner jobs;  // goes on when moving to another directory; last, so it stops first

    std::size_t rowCount() const { return files.selected().size(); }

    void selectOnly(BrowseContext* owner, int index);
    void apply(BrowseContext* owner, const std::vector<char>& selected);
    void moveCurrent(BrowseContext* owner, int index);

    // Marks the rows from the anchor to index, in addition to those in base.
    std::vector<char> withRange(std::vector<char> base, int index) const;

    // Shows a file or directory, without recording it in the history.
    void open(BrowseContext* owner, const QString& path);

    // Moves through the history: takes the entry so many steps away on one stack, counting those
    // that still exist, and leaves where we are and what it passed on the other.
    void travel(BrowseContext* owner, QList<HistoryEntry>& from, QList<HistoryEntry>& to, int steps = 1);

    // Scans the directory. Names in toSelect, if present then, become the selection.
    void scan(BrowseContext* owner, const QStringList& toSelect = {});

    void applyScan(BrowseContext* owner, const QList<FileEntry>& entries, const QStringList& toSelect);

    // Starts a job unless one is running. done runs on the main thread when it ends, and by
    // default the directory is scanned and files new in it are selected.
    void startJob(BrowseContext* owner, const QString& name, int total, FileJobRunner::Work work, Done done = {});
};

}  // namespace iv
