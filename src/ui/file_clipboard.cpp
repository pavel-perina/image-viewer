#include "ui/file_clipboard.h"

#include <QClipboard>
#include <QDir>
#include <QDrag>
#include <QFileInfo>
#include <QGuiApplication>
#include <QList>
#include <QMimeData>
#include <QPixmap>
#include <QTimer>
#include <QUrl>

#include <cstring>

#include "core/thumbnail.h"
#include "ui/qimage_bridge.h"

namespace iv {

namespace {

constexpr int kDecodePriority = 100;
// A clipboard image is for pasting elsewhere. Past this edge a pyramid gives a reduced version.
constexpr int kClipboardImageEdge = 16384;

// Files for other applications. File managers on GNOME look for the second format.
// How file managers mark files cut rather than copied, so that the one that pastes moves them.
constexpr const char* kKdeCut = "application/x-kde-cutselection";
constexpr const char* kGnomeFiles = "x-special/gnome-copied-files";
#ifdef Q_OS_WIN
constexpr const char* kWindowsDropEffect = "application/x-qt-windows-mime;value=\"Preferred DropEffect\"";
constexpr quint32 kDropEffectCopy = 1;
constexpr quint32 kDropEffectMove = 2;
#endif

QMimeData* filesMimeData(const QStringList& paths, bool cut = false) {
    auto* mime = new QMimeData;
    QList<QUrl> urls;
    QByteArray gnomeFiles = cut ? "cut" : "copy";
    for (const QString& path : paths) {
        const QUrl url = QUrl::fromLocalFile(path);
        urls << url;
        gnomeFiles += '\n' + url.toEncoded();
    }
    mime->setUrls(urls);
    mime->setData(QString::fromLatin1(kGnomeFiles), gnomeFiles);
    if (cut) {
        mime->setData(QString::fromLatin1(kKdeCut), "1");
    }
#ifdef Q_OS_WIN
    // A DWORD, as Explorer writes it.
    const quint32 effect = cut ? kDropEffectMove : kDropEffectCopy;
    mime->setData(QString::fromLatin1(kWindowsDropEffect), QByteArray(reinterpret_cast<const char*>(&effect), sizeof effect));
#endif
    return mime;
}

}  // namespace

FileClipboard::FileClipboard(QObject* parent) : QObject(parent) {
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &FileClipboard::look);
    QTimer::singleShot(0, this, &FileClipboard::look);  // not while the window is being made
}

FileClipboard::~FileClipboard() {
    // A decoded image is posted to this object, so the decoding must finish first.
    m_queue.cancelGroup(m_group);
    m_queue.waitGroup(m_group);
}

void FileClipboard::copyFiles(const QStringList& paths) {
    if (paths.isEmpty()) {
        return;
    }
    QClipboard* clipboard = QGuiApplication::clipboard();
    QMimeData* mime = filesMimeData(paths);
    clipboard->setMimeData(mime);
    if (paths.size() != 1) {
        return;
    }
    // Decoded here rather than when asked for, as a clipboard manager asks at once, and that
    // would stop the main thread. Colours are converted to sRGB, which is what others assume.
    m_queue.submit(m_group, kDecodePriority, [this, mime, path = paths.front()](std::stop_token stop) {
        const auto image = makeThumbnail(QFileInfo(path).filesystemFilePath(), kClipboardImageEdge, stop);
        if (!image) {
            return;  // it still is on the clipboard as a file
        }
        QImage pixels = toQImage(*image);
        QMetaObject::invokeMethod(this, [mime, path, pixels = std::move(pixels)] {
            QClipboard* board = QGuiApplication::clipboard();
            // The pointer is only compared: if anything else was copied since, it is gone.
            if (board->mimeData() == mime) {
                QMimeData* withImage = filesMimeData({path});
                withImage->setImageData(pixels);
                board->setMimeData(withImage);
            }
        });
    });
}

void FileClipboard::cutFiles(const QStringList& paths) {
    if (!paths.isEmpty()) {
        QGuiApplication::clipboard()->setMimeData(filesMimeData(paths, true));
    }
}

void FileClipboard::copyPaths(const QStringList& paths) {
    QStringList native;
    for (const QString& path : paths) {
        native << QDir::toNativeSeparators(path);
    }
    QGuiApplication::clipboard()->setText(native.join(QLatin1Char('\n')));
}

void FileClipboard::copyText(const QString& text) { QGuiApplication::clipboard()->setText(text); }

QStringList FileClipboard::files() const {
    QStringList paths;
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if (mime && mime->hasUrls()) {
        for (const QUrl& url : mime->urls()) {
            if (url.isLocalFile()) {
                paths << url.toLocalFile();
            }
        }
    }
    return paths;
}

bool FileClipboard::isCut() const {
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if (!mime) {
        return false;
    }
    if (mime->data(QString::fromLatin1(kKdeCut)).startsWith('1') || mime->data(QString::fromLatin1(kGnomeFiles)).startsWith("cut")) {
        return true;
    }
#ifdef Q_OS_WIN
    const QByteArray effect = mime->data(QString::fromLatin1(kWindowsDropEffect));
    if (effect.size() >= 4) {
        quint32 value = 0;
        std::memcpy(&value, effect.constData(), sizeof value);
        return (value & kDropEffectMove) != 0;
    }
#endif
    return false;
}

bool FileClipboard::canPaste() const { return m_canPaste; }

void FileClipboard::look() {
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    m_canPaste = mime && (mime->hasImage() || !files().isEmpty());
    emit changed();  // also for what files() and isCut() say, which may differ with the same answer
}

QString FileClipboard::text() const { return QGuiApplication::clipboard()->text(); }

void FileClipboard::clear() { QGuiApplication::clipboard()->clear(); }

void FileClipboard::dragFiles(const QStringList& paths, const QImage& image) {
    if (paths.isEmpty()) {
        return;
    }
    auto* mime = new QMimeData;
    QList<QUrl> urls;
    for (const QString& path : paths) {
        urls << QUrl::fromLocalFile(path);
    }
    mime->setUrls(urls);
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    if (!image.isNull()) {
        drag->setPixmap(QPixmap::fromImage(image));
        drag->setHotSpot(QPoint(-12, -12));  // beside the pointer, leaving where it goes in sight
    }
    drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
}

}  // namespace iv
