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

#include <algorithm>
#include <optional>
#include <type_traits>
#include <variant>

#include "core/image_writer.h"
#include "core/jpeg_transform.h"
#include "core/save_image.h"
#ifdef IV_HAVE_JXL
#include "core/jxl_transcode.h"
#endif

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

FileJobResult editJpegFiles(const QStringList& paths, JpegEdit edit, bool trimEdges, std::stop_token stop,
                            const JobProgress& progress) {
    FileJobResult result;
    int done = 0;
    for (const QString& path : paths) {
        if (stop.stop_requested()) {
            break;
        }
        progress(done++);
        const QFileInfo info(path);
        const Result<bool> changed = transformJpegFile(info.filesystemFilePath(), edit, trimEdges);
        if (!changed) {
            result.problems << problem(info.fileName(), QString::fromStdString(changed.error()));
        } else if (*changed) {
            result.created << path;
        }
    }
    progress(done);
    return result;
}

bool canConvertToJxl() {
#ifdef IV_HAVE_JXL
    return true;
#else
    return false;
#endif
}

FileJobResult convertJpegFilesToJxl(const QStringList& paths, bool trashOriginals, std::stop_token stop,
                                    const JobProgress& progress) {
    FileJobResult result;
    int done = 0;
    for (const QString& path : paths) {
        if (stop.stop_requested()) {
            break;
        }
        progress(done++);
        const QFileInfo info(path);
#ifdef IV_HAVE_JXL
        const Result<std::filesystem::path> created = convertJpegFileToJxl(info.filesystemFilePath());
        if (!created) {
            result.problems << problem(info.fileName(), QString::fromStdString(created.error()));
            continue;
        }
        result.created << QFileInfo(*created).absoluteFilePath();
        if (trashOriginals) {
            QFile file(path);
            if (!file.moveToTrash()) {
                result.problems << problem(info.fileName(), QObject::tr("converted, but cannot be moved to the trash: %1").arg(file.errorString()));
            }
        }
#else
        Q_UNUSED(trashOriginals);
        result.problems << problem(info.fileName(), QObject::tr("this build cannot write JPEG XL"));
#endif
    }
    progress(done);
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

namespace {

ChromaSubsampling chromaFrom(const QVariantMap& options, ChromaSubsampling fallback) {
    const QString chroma = options.value(QStringLiteral("chroma")).toString();
    if (chroma == QLatin1String("4:4:4")) {
        return ChromaSubsampling::Full;
    }
    if (chroma == QLatin1String("4:2:2")) {
        return ChromaSubsampling::Horizontal;
    }
    if (chroma == QLatin1String("4:2:0")) {
        return ChromaSubsampling::Both;
    }
    return fallback;
}

int intFrom(const QVariantMap& options, const char* key, int fallback) {
    const QVariant value = options.value(QLatin1String(key));
    return value.isValid() ? value.toInt() : fallback;
}

bool boolFrom(const QVariantMap& options, const char* key, bool fallback) {
    const QVariant value = options.value(QLatin1String(key));
    return value.isValid() ? value.toBool() : fallback;
}

// The Butteraugli distance libjxl's cjxl uses for a quality, so that 90 means distance 1.
float jxlDistanceFor(int quality) {
    const double q = std::clamp(quality, 1, 100);
    return static_cast<float>(q >= 30 ? 0.1 + (100 - q) * 0.09 : 53.0 / 3000 * q * q - 23.0 / 20 * q + 25);
}

// The writer's options for the format the target names, adjusted by what the dialog chose.
std::optional<WriteOptions> writeOptionsFor(const QString& target, const QVariantMap& options) {
    auto chosen = optionsForExtension(QFileInfo(target).filesystemFilePath());
    if (!chosen) {
        return std::nullopt;
    }
    std::visit(
        [&](auto& o) {
            using T = std::decay_t<decltype(o)>;
            if constexpr (std::is_same_v<T, JpegWriteOptions>) {
                o.quality = intFrom(options, "quality", o.quality);
                o.chroma = chromaFrom(options, o.chroma);
                o.progressive = boolFrom(options, "progressive", o.progressive);
            } else if constexpr (std::is_same_v<T, PngWriteOptions>) {
                o.compressionLevel = intFrom(options, "compressionLevel", o.compressionLevel);
            } else if constexpr (std::is_same_v<T, TiffWriteOptions>) {
                const QString compression = options.value(QStringLiteral("compression")).toString();
                if (compression == QLatin1String("none")) {
                    o.compression = TiffCompression::None;
                } else if (compression == QLatin1String("lzw")) {
                    o.compression = TiffCompression::Lzw;
                } else if (compression == QLatin1String("deflate")) {
                    o.compression = TiffCompression::Deflate;
                }
            } else if constexpr (std::is_same_v<T, WebpWriteOptions>) {
                o.lossless = boolFrom(options, "lossless", o.lossless);
                o.quality = intFrom(options, "quality", o.quality);
                o.effort = intFrom(options, "effort", o.effort);
            } else if constexpr (std::is_same_v<T, AvifWriteOptions>) {
                o.quality = intFrom(options, "quality", o.quality);
                o.speed = intFrom(options, "speed", o.speed);
                o.chroma = chromaFrom(options, o.chroma);
            } else if constexpr (std::is_same_v<T, JxlWriteOptions>) {
                o.lossless = boolFrom(options, "lossless", o.lossless);
                if (options.contains(QStringLiteral("quality"))) {
                    o.distance = jxlDistanceFor(options.value(QStringLiteral("quality")).toInt());
                }
                o.effort = intFrom(options, "effort", o.effort);
            }
        },
        *chosen);
    return chosen;
}

}  // namespace

QString saveFormatFor(const QString& path) {
    const auto options = optionsForExtension(QFileInfo(path).filesystemFilePath());
    if (!options || !canWrite(*options)) {
        return {};
    }
    static constexpr const char* kNames[] = {"jpeg", "png", "tiff", "webp", "avif", "jxl"};
    return QLatin1String(kNames[options->index()]);
}

FileJobResult saveImageAs(const QString& source, int page, const QString& target, const QVariantMap& options,
                          std::stop_token stop) {
    FileJobResult result;
    const QString name = QFileInfo(target).fileName();
    const auto writeOptions = writeOptionsFor(target, options);
    if (!writeOptions || !canWrite(*writeOptions)) {
        result.problems << QObject::tr("%1: this format cannot be written").arg(name);
        return result;
    }
    auto opened = openImageSource(QFileInfo(source).filesystemFilePath());
    if (!opened) {
        result.problems << QObject::tr("%1: %2").arg(QFileInfo(source).fileName(), QString::fromStdString(opened.error()));
        return result;
    }
    const SaveOptions saveOptions{.keepMetadata = boolFrom(options, "keepMetadata", true),
                                  .removeLocation = boolFrom(options, "removeLocation", false)};
    const auto saved = saveSubimage(**opened, page, QFileInfo(target).filesystemFilePath(), *writeOptions, saveOptions, true, stop);
    if (!saved) {
        result.problems << QObject::tr("%1: %2").arg(name, QString::fromStdString(saved.error()));
        return result;
    }
    result.created << QFileInfo(target).absoluteFilePath();
    return result;
}

}  // namespace iv
