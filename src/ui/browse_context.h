#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QAbstractItemModel;
Q_MOC_INCLUDE(<QAbstractItemModel>)

namespace iv {

// One browsing context: a directory, its images, the current one and the selection. Everything
// a tab or file panel owns lives here, so several can exist side by side.
class BrowseContext : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString directory READ directory NOTIFY directoryChanged)
    Q_PROPERTY(QVariantList breadcrumbs READ breadcrumbs NOTIFY directoryChanged)
    Q_PROPERTY(QAbstractItemModel* files READ files CONSTANT)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY filesChanged)
    Q_PROPERTY(bool hasNext READ hasNext NOTIFY currentChanged)
    Q_PROPERTY(bool hasPrevious READ hasPrevious NOTIFY currentChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(QStringList selectedPaths READ selectedPaths NOTIFY selectionChanged)
    Q_PROPERTY(SortKey sortKey READ sortKey WRITE setSortKey NOTIFY sortChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY sortChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY jobChanged)
    Q_PROPERTY(QString jobName READ jobName NOTIFY jobChanged)
    Q_PROPERTY(int jobDone READ jobDone NOTIFY jobChanged)
    Q_PROPERTY(int jobTotal READ jobTotal NOTIFY jobChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY directoryChanged)
    // "13 directories | 311 files (500 MB) | 1 selected (780 kB)"
    Q_PROPERTY(QString summary READ summary NOTIFY summaryChanged)
    Q_PROPERTY(bool canConvertToJxl READ canConvertToJxl CONSTANT)

public:
    enum SortKey { SortByName, SortByDate, SortBySize, SortByType };
    Q_ENUM(SortKey)
    enum Conflict { SkipExisting, ReplaceExisting, KeepBoth };
    Q_ENUM(Conflict)
    // Lossless edits of JPEG files, applied to the image as shown.
    enum JpegOperation { RotateClockwise, RotateCounterclockwise, Rotate180, FlipHorizontal, FlipVertical, ApplyOrientation };
    Q_ENUM(JpegOperation)

    explicit BrowseContext(QObject* parent = nullptr);
    ~BrowseContext() override;

    QString directory() const;
    // One {name, path} map per level of the directory, starting at the root.
    QVariantList breadcrumbs() const;
    // Images of the directory, with the roles name, path, thumbnail and selected.
    QAbstractItemModel* files() const;
    QString currentPath() const;
    QString currentName() const;
    int currentIndex() const;
    int fileCount() const;
    bool hasNext() const;
    bool hasPrevious() const;
    int selectedCount() const;
    QStringList selectedPaths() const;  // in the order of the file list
    SortKey sortKey() const;
    void setSortKey(SortKey key);
    bool sortDescending() const;
    void setSortDescending(bool descending);
    bool busy() const;  // while a file job runs; another cannot start until it ends
    QString jobName() const;
    int jobDone() const;
    int jobTotal() const;
    bool canPaste() const;
    bool canGoBack() const;
    bool canGoForward() const;
    bool canGoUp() const;
    QString summary() const;

    // Opens an image file and scans its directory, or scans a directory and opens its
    // first image. Scanning happens in the background.
    Q_INVOKABLE void openPath(const QString& path);
    Q_INVOKABLE void openUrl(const QUrl& url);
    // Moving to another image selects only that image.
    Q_INVOKABLE void setCurrentIndex(int index);
    // A click on a file. Plain selects only it, ctrl toggles it, and shift selects the range
    // from the anchor, the file last clicked without shift. Ctrl and shift adds that range.
    Q_INVOKABLE void clickFile(int index, bool ctrl, bool shift);
    // Moves the current image and selects from the anchor to it, as shift and an arrow key do.
    Q_INVOKABLE void extendSelectionTo(int index);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();  // the current image stays current
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    // Through the folders visited, returning to the image that was current in each. Folders
    // that no longer exist are passed over.
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void goUp();
    // The folders goBack would pass through that still exist, the nearest first, and going to one
    // of them at once: 1 is the same as goBack.
    Q_INVOKABLE QStringList backFolders() const;
    Q_INVOKABLE void goBackBy(int steps);
    // Opens what the user typed or pasted: a folder or file, absolute or relative to the
    // directory, with ~, quotes and file:// URLs understood. Returns why it cannot be opened,
    // or an empty string if it was.
    Q_INVOKABLE QString openTyped(const QString& text);
    // Scans the directory again, keeping the current image and selection by name. If the
    // current image is gone, the one that took its place becomes current. Changes on disk also
    // do this by themselves where the file system reports them.
    Q_INVOKABLE void refresh();

    // File jobs run in the background and end with operationFinished. The folder is scanned
    // afterwards, and new files in it become the selection.
    Q_INVOKABLE void trash(const QStringList& paths);
    Q_INVOKABLE void deletePermanently(const QStringList& paths);  // the caller confirms first
    Q_INVOKABLE void copyTo(const QStringList& paths, const QString& folder, Conflict conflict);
    Q_INVOKABLE void moveTo(const QStringList& paths, const QString& folder, Conflict conflict);
    Q_INVOKABLE int conflictCount(const QStringList& paths, const QString& folder) const;
    // These return why the name cannot be used, or an empty string if the job has started.
    Q_INVOKABLE QString rename(const QString& path, const QString& newName);
    // Why a new file of that name cannot be made in the folder, or empty.
    Q_INVOKABLE QString newFileProblem(const QString& folder, const QString& name) const;
    Q_INVOKABLE QString createFolder(const QString& parent, const QString& name);
    Q_INVOKABLE void trashFolder(const QString& path);
    // Whether every path names a JPEG file, judging by the extension. False for none.
    Q_INVOKABLE bool allJpeg(const QStringList& paths) const;
    // Changes the files in place; the caller confirms first.
    Q_INVOKABLE void editJpegs(const QStringList& paths, JpegOperation operation, bool trimEdges);
    Q_INVOKABLE void convertToJxl(const QStringList& paths, bool trashOriginals);
    // "jpeg", "png", "tiff", "webp", "avif" or "jxl" for a file name to save as, or empty if
    // its extension names nothing this build can write.
    Q_INVOKABLE QString saveFormat(const QString& path) const;
    // One {label, extensions} per format this build can write, for the file chooser; the first
    // extension is the one a new name gets.
    Q_INVOKABLE QVariantList saveTypes() const;
    // Writes a page of an image to a file in the format of its extension; the options are
    // those saveImageAs takes. The new file becomes the current one.
    // The page of the source file, or the edited image from ImageView::editedImage if valid.
    Q_INVOKABLE void saveAs(const QString& source, int page, const QString& target, const QVariantMap& options,
                            const QVariant& edited = {});
    bool canConvertToJxl() const;
    Q_INVOKABLE void cancelJob();  // stops before the next file

    // Puts the files on the clipboard for other applications, and a single file also as an
    // image, which arrives there a moment later as it has to be decoded.
    Q_INVOKABLE void copyFilesToClipboard(const QStringList& paths);
    Q_INVOKABLE void copyPathsToClipboard(const QStringList& paths);
    Q_INVOKABLE void copyTextToClipboard(const QString& text);
    // Local files on the clipboard, which take precedence over an image there.
    Q_INVOKABLE QStringList clipboardFiles() const;
    // Saves the image on the clipboard as a new file in the directory.
    Q_INVOKABLE void pasteImage();
    Q_INVOKABLE QString clipboardText() const;

    // Drags the files out, to another application or a folder of the tree, until the button is
    // released. A copy unless Ctrl, Shift or the other side asks for a move, as in file managers;
    // Qt Quick's own drag would make a move the default. A moved file is never deleted here: the
    // other side moves it, or it stays a copy. The image, when not null, is shown by the pointer.
    Q_INVOKABLE void dragFiles(const QStringList& paths, const QImage& image);

    Q_INVOKABLE QString pathFromUrl(const QUrl& url) const;
    Q_INVOKABLE QUrl urlFromPath(const QString& path) const;
    Q_INVOKABLE QString nativePath(const QString& path) const;
    Q_INVOKABLE QString dataSizeText(double bytes) const;  // "780 kB", "1.5 MB"

signals:
    void directoryChanged();
    void currentChanged();
    void filesChanged();
    void selectionChanged();
    void sortChanged();
    void jobChanged();
    void clipboardChanged();
    void historyChanged();
    void summaryChanged();
    // The last image of the directory went away.
    void emptied();
    void operationFinished(const QStringList& problems);
    // These files have new contents, so whatever shows them should load them again.
    void filesEdited(const QStringList& paths);
    // An edited image from saveAs was written.
    void editedImageSaved(const QVariant& edited);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace iv
