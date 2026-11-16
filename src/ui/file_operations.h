#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <stop_token>

class QDateTime;
class QImage;

namespace iv {

enum class JpegEdit;
class ImageSource;

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

// Turns or mirrors JPEG files in place without loss, as jpeg_transform.h describes. created
// lists the files that changed.
FileJobResult editJpegFiles(const QStringList& paths, JpegEdit edit, bool trimEdges, std::stop_token stop,
                            const JobProgress& progress);

// Whether this build can write JPEG XL.
bool canConvertToJxl();

// Writes a JPEG XL file next to each JPEG, each checked to give back the original exactly. With
// trashOriginals, each JPEG whose conversion succeeded then goes to the trash.
FileJobResult convertJpegFilesToJxl(const QStringList& paths, bool trashOriginals, std::stop_token stop,
                                    const JobProgress& progress);

// The format a file name's extension asks for: "jpeg", "png", "tiff", "webp", "avif" or "jxl",
// or empty if this build cannot write it.
QString saveFormatFor(const QString& path);

// Writes a page of an image to a file in the format its extension names, replacing one that is
// there. The options come as the save dialog gives them: quality (1-100), chroma ("4:4:4",
// "4:2:2" or "4:2:0"), progressive, lossless, effort (0-9, the format's own range), compression
// for TIFF ("none", "lzw", "deflate") or the level for PNG (0-9), keepMetadata and
// removeLocation. What is missing takes the writer's default. An edited page, if given, is
// saved instead of the page of the source file.
FileJobResult saveImageAs(const QString& source, int page, const QString& target, const QVariantMap& options,
                          std::stop_token stop, std::shared_ptr<ImageSource> edited = nullptr);

// A name for a new file from the time it is made, such as 2026-12-24_125959, without extension.
QString timeStampName(const QDateTime& now);

// Saves as a new file named after the time, such as 2026-12-24_125959.png, adding _1 and so on
// if that name is taken.
FileJobResult saveNewImage(const QImage& image, const QString& folder, const QDateTime& now);

}  // namespace iv
