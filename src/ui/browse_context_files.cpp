// The file jobs of BrowseContext: copying, moving, renaming, deleting, editing and saving.

#include "ui/browse_context.h"
#include "ui/browse_context_p.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QVariantMap>

#include <algorithm>
#include <memory>
#include <utility>

#include "core/image_source.h"
#include "core/jpeg_transform.h"
#include "ui/edited_image.h"
#include "ui/file_operations.h"

namespace iv {

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

void BrowseContext::cancelJob() { m_impl->jobs.cancel(); }

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

}  // namespace iv
