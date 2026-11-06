#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <stop_token>

class QDateTime;
class QImage;

namespace iv {

// What to do when a file with the same name is already in the target folder.
enum class ConflictPolicy {
    Skip,
    Replace,   // the existing file goes to the trash
    KeepBoth,  // the new file becomes "name (1).ext"
};

struct FileJobResult {
    QStringList created;   // full paths of the new files
    QStringList problems;  // one "name: reason" per file that failed
};

// Called with the number of files finished. These functions block, so call them from a worker.
using JobProgress = std::function<void(int done)>;

// Why this cannot be a file or folder name, or empty if it can.
QString fileNameProblem(const QString& name);

// A name free in the folder: the given one, or else "name (1).ext" and so on.
QString freeFileName(const QString& folder, const QString& name);

// How many of the files would meet another file of the same name in the folder. A file that
// is already there does not count, as it cannot replace itself.
int countConflicts(const QStringList& paths, const QString& folder);

FileJobResult trashFiles(const QStringList& paths, std::stop_token stop, const JobProgress& progress);

// Removes files for good, bypassing the trash. Files only, so a folder is reported as a problem.
FileJobResult deleteFiles(const QStringList& paths, std::stop_token stop, const JobProgress& progress);

// A file copied onto itself gets a free name, and one moved onto itself stays where it is,
// whatever the policy. An interrupted copy leaves no partial file behind.
FileJobResult copyFiles(const QStringList& paths, const QString& folder, ConflictPolicy policy, std::stop_token stop,
                        const JobProgress& progress);
FileJobResult moveFiles(const QStringList& paths, const QString& folder, ConflictPolicy policy, std::stop_token stop,
                        const JobProgress& progress);

// Works for files and folders, and never replaces an existing one.
FileJobResult renamePath(const QString& path, const QString& newName);
FileJobResult createFolder(const QString& parent, const QString& name);

// Saves as a new file named after the time, such as 2026-12-24_125959.png, adding _1 and so on
// if that name is taken.
FileJobResult saveNewImage(const QImage& image, const QString& folder, const QDateTime& now);

}  // namespace iv
