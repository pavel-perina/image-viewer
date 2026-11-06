#include "ui/file_operations.h"

#include <QBuffer>
#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QObject>
#include <QTimeZone>

namespace iv {

namespace {

// PNG keeps what was copied exactly. This becomes a setting with the configuration dialog.
constexpr const char* kPasteFormat = "png";

bool sameFile(const QFileInfo& a, const QFileInfo& b) {
    return a.exists() && b.exists() && a.canonicalFilePath() == b.canonicalFilePath();
}

QString problem(const QString& name, const QString& reason) { return name + QStringLiteral(": ") + reason; }

// QFile::copy does not keep the time, and a copied photo should still sort by when it was taken.
void keepModificationTime(const QString& path, const QDateTime& modified) {
    QFile file(path);
    if (file.open(QIODevice::ReadWrite)) {
        file.setFileTime(modified, QFileDevice::FileModificationTime);
    }
}

FileJobResult transfer(const QStringList& paths, const QString& folder, ConflictPolicy policy, bool move,
                       const std::stop_token& stop, const JobProgress& progress) {
    FileJobResult result;
    const QDir directory(folder);
    int done = 0;
    for (const QString& path : paths) {
        if (stop.stop_requested()) {
            break;
        }
        progress(done++);

        const QFileInfo source(path);
        const QString name = source.fileName();
        if (source.isDir()) {
            result.problems << problem(name, QObject::tr("folders cannot be copied or moved yet"));
            continue;
        }
        QString target = directory.filePath(name);
        const QFileInfo existing(target);
        if (sameFile(source, existing)) {
            if (move) {
                continue;
            }
            target = directory.filePath(freeFileName(folder, name));
        } else if (existing.exists()) {
            if (policy == ConflictPolicy::Skip) {
                continue;
            }
            if (policy == ConflictPolicy::KeepBoth) {
                target = directory.filePath(freeFileName(folder, name));
            } else {
                if (existing.isDir()) {
                    result.problems << problem(name, QObject::tr("a folder in the target has this name"));
                    continue;
                }
                QFile old(target);
                if (!old.moveToTrash()) {
                    result.problems << problem(name, QObject::tr("cannot move the existing file to the trash: ") + old.errorString());
                    continue;
                }
            }
        }

        const QDateTime modified = source.lastModified(QTimeZone::UTC);
        QFile file(path);
        if (!(move ? file.rename(target) : file.copy(target))) {
            result.problems << problem(name, file.errorString());
            continue;
        }
        keepModificationTime(target, modified);
        result.created << target;
    }
    progress(done);
    return result;
}

}  // namespace

QString fileNameProblem(const QString& name) {
    if (name.trimmed().isEmpty()) {
        return QObject::tr("The name is empty.");
    }
    if (name == QStringLiteral(".") || name == QStringLiteral("..")) {
        return QObject::tr("This name is reserved.");
    }
#ifdef Q_OS_WIN
    const QString forbidden = QStringLiteral("/\\<>:\"|?*");
#else
    const QString forbidden = QStringLiteral("/");
#endif
    for (const QChar c : forbidden) {
        if (name.contains(c)) {
            return QObject::tr("The name cannot contain %1").arg(c);
        }
    }
    return {};
}

QString freeFileName(const QString& folder, const QString& name) {
    const QDir directory(folder);
    if (!QFileInfo::exists(directory.filePath(name))) {
        return name;
    }
    const QFileInfo info(name);
    const QString suffix = info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix();
    for (int n = 1;; ++n) {
        const QString candidate = QStringLiteral("%1 (%2)%3").arg(info.completeBaseName()).arg(n).arg(suffix);
        if (!QFileInfo::exists(directory.filePath(candidate))) {
            return candidate;
        }
    }
}

int countConflicts(const QStringList& paths, const QString& folder) {
    const QDir directory(folder);
    int count = 0;
    for (const QString& path : paths) {
        const QFileInfo source(path);
        const QFileInfo existing(directory.filePath(source.fileName()));
        if (existing.exists() && !sameFile(source, existing)) {
            ++count;
        }
    }
    return count;
}

FileJobResult trashFiles(const QStringList& paths, std::stop_token stop, const JobProgress& progress) {
    FileJobResult result;
    int done = 0;
    for (const QString& path : paths) {
        if (stop.stop_requested()) {
            break;
        }
        progress(done++);
        QFile file(path);
        if (!file.moveToTrash()) {
            result.problems << problem(QFileInfo(path).fileName(), file.errorString());
        }
    }
    progress(done);
    return result;
}

FileJobResult deleteFiles(const QStringList& paths, std::stop_token stop, const JobProgress& progress) {
    FileJobResult result;
    int done = 0;
    for (const QString& path : paths) {
        if (stop.stop_requested()) {
            break;
        }
        progress(done++);
        const QFileInfo info(path);
        if (info.isDir() && !info.isSymLink()) {
            result.problems << problem(info.fileName(), QObject::tr("folders cannot be deleted permanently"));
            continue;
        }
        QFile file(path);
        if (!file.remove()) {
            result.problems << problem(info.fileName(), file.errorString());
        }
    }
    progress(done);
    return result;
}

FileJobResult copyFiles(const QStringList& paths, const QString& folder, ConflictPolicy policy, std::stop_token stop,
                        const JobProgress& progress) {
    return transfer(paths, folder, policy, false, stop, progress);
}

FileJobResult moveFiles(const QStringList& paths, const QString& folder, ConflictPolicy policy, std::stop_token stop,
                        const JobProgress& progress) {
    return transfer(paths, folder, policy, true, stop, progress);
}

FileJobResult renamePath(const QString& path, const QString& newName) {
    FileJobResult result;
    const QFileInfo info(path);
    const QString target = info.dir().filePath(newName);
    QFile file(path);
    if (file.rename(target)) {
        result.created << target;
    } else {
        result.problems << problem(info.fileName(), file.errorString());
    }
    return result;
}

FileJobResult createFolder(const QString& parent, const QString& name) {
    FileJobResult result;
    const QDir directory(parent);
    if (QFileInfo::exists(directory.filePath(name))) {
        result.problems << problem(name, QObject::tr("it already exists"));
    } else if (directory.mkdir(name)) {
        result.created << directory.filePath(name);
    } else {
        result.problems << problem(name, QObject::tr("cannot create the folder"));
    }
    return result;
}

FileJobResult saveNewImage(const QImage& image, const QString& folder, const QDateTime& now) {
    FileJobResult result;
    const QString stamp = now.toString(QStringLiteral("yyyy-MM-dd_HHmmss"));
    const QString suffix = QLatin1Char('.') + QLatin1StringView(kPasteFormat);

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, kPasteFormat)) {
        result.problems << problem(stamp + suffix, QObject::tr("cannot encode the image"));
        return result;
    }

    const QDir directory(folder);
    for (int n = 0;; ++n) {
        const QString name = n == 0 ? stamp + suffix : QStringLiteral("%1_%2%3").arg(stamp).arg(n).arg(suffix);
        const QString path = directory.filePath(name);
        QFile file(path);
        // NewOnly fails rather than replacing a file that appeared since the check.
        if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            if (file.write(bytes) == bytes.size() && file.flush()) {
                result.created << path;
            } else {
                result.problems << problem(name, file.errorString());
                file.remove();
            }
            return result;
        }
        if (!QFileInfo::exists(path)) {
            result.problems << problem(name, file.errorString());
            return result;
        }
    }
}

}  // namespace iv
